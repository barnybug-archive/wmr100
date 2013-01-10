/**
 * Oregon Scientific WMR100/200 protocol. Tested on wmr100.
 *
 * Copyright 2009 Barnaby Gray <barnaby@pickle.me.uk>
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <hid.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <sqlite3.h>
#include <pthread.h>

#define WMR100_VENDOR_ID  0x0fde
#define WMR100_PRODUCT_ID 0xca01

/****************************
 Variables definition
****************************/

/* globals */

#define OUTPUT_BOTH 3
#define OUTPUT_FILE 2
#define OUTPUT_STDOUT 1

int gOutput = OUTPUT_BOTH;

/* constants */

#define MAXSENSORS 5
#define RECORD_HISTORY 60

int const RECV_PACKET_LEN   = 8;
int const BUF_SIZE = 255;
unsigned char const PATHLEN = 2;
int const PATH_IN[]  = { 0xff000001, 0xff000001 };
int const PATH_OUT[] = { 0xff000001, 0xff000002 };
unsigned char const INIT_PACKET1[] = { 0x20, 0x00, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00 };
unsigned char const INIT_PACKET2[] = { 0x01, 0xd0, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00 };

char *const SMILIES[] = { "  ", ":D", ":(", ":|" };
char *const TRENDS[] = { "-", "U", "D" };
char *const WINDIES[] = { "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NWN" };

/* WMR */

typedef struct _WMR {
    int pos;
    int remain;
    unsigned char* buffer;
    HIDInterface *hid;
    FILE *data_fh;
    char *data_filename;
} WMR;

WMR *wmr = NULL;

/* Sqlite loggin */

typedef struct _TEMP {

    float temp;
    int smile;
    int humidity;
    float dewpoint;
    char *trend;
} TEMP;

typedef struct _WATER {

    float temp;
} WATER;

typedef struct _CURRENTCONDITION {

    int pressure;
    int forecast;
    int rain_rate;
    float rain_hour_total;
    float rain_day_total;
    float rain_all_total;
    char *wind_dir;
    float wind_speed;
    float wind_avg_speed;
    int uv;
    WATER water[MAXSENSORS];
    TEMP temp[MAXSENSORS];
} CURRENTCONDITION;

CURRENTCONDITION *currentcondition = NULL;

/* SQLITE WRITTER THREAD */

pthread_t sqliteLoggerThread;
pthread_mutex_t currentcondition_lock;

/****************************
 Dump packet
****************************/

void dump_packet(unsigned char *packet, int len){
    int i;

    printf("Receive packet len %d: ", len);
    for(i = 0; i < len; ++i)
        printf("%02x ", (int)packet[i]);
    printf("\n");
}

/****************************
  WMR methods
 ****************************/

WMR *wmr_new(){
    WMR *wmr = malloc(sizeof(WMR));
    wmr->remain = 0;
    wmr->buffer = malloc(BUF_SIZE);
    if (wmr->buffer == NULL) {
      free(wmr);
      return NULL;
    }
    wmr->data_fh = NULL;
    wmr->data_filename = "./data.log";
    return wmr;
}

void wmr_send_packet_init(WMR *wmr){
    int ret;

    ret = hid_set_output_report(wmr->hid, PATH_IN, PATHLEN, (char*)INIT_PACKET1, sizeof(INIT_PACKET1));
    if (ret != HID_RET_SUCCESS) {
        fprintf(stderr, "hid_set_output_report failed with return code %d\n", ret);
        return;
    }
}

void wmr_send_packet_ready(WMR *wmr){
    int ret;
    
    ret = hid_set_output_report(wmr->hid, PATH_IN, PATHLEN, (char*)INIT_PACKET2, sizeof(INIT_PACKET2));
    if (ret != HID_RET_SUCCESS) {
        fprintf(stderr, "hid_set_output_report failed with return code %d\n", ret);
        return;
    }
}

int wmr_init(WMR *wmr){
    hid_return ret;
    HIDInterfaceMatcher matcher = { WMR100_VENDOR_ID, WMR100_PRODUCT_ID, NULL, NULL, 0 };
    int retries;

    /* see include/debug.h for possible values */
    /*hid_set_debug(HID_DEBUG_ALL);*/
    /*hid_set_debug_stream(stderr);*/
    /* passed directly to libusb */
    /*hid_set_usb_debug(0);*/
  
    ret = hid_init();
    if (ret != HID_RET_SUCCESS) {
        fprintf(stderr, "hid_init failed with return code %d\n", ret);
        return 1;
    }

    wmr->hid = hid_new_HIDInterface();
    if (wmr->hid == 0) {
        fprintf(stderr, "hid_new_HIDInterface() failed, out of memory?\n");
        return 1;
    }

    retries = 5;
    while(retries > 0) {
        ret = hid_force_open(wmr->hid, 0, &matcher, 10);
        if (ret == HID_RET_SUCCESS) break;

        fprintf(stderr, "Open failed, sleeping 5 seconds before retrying..\n");
        sleep(5);

        --retries;
    }
    if (ret != HID_RET_SUCCESS) {
      fprintf(stderr, "hid_force_open failed with return code %d\n", ret);
      return 1;
    }
    
    ret = hid_write_identification(stdout, wmr->hid);
    if (ret != HID_RET_SUCCESS) {
        fprintf(stderr, "hid_write_identification failed with return code %d\n", ret);
        return 1;
    }

    wmr_send_packet_init(wmr);
    wmr_send_packet_ready(wmr);
    return 0;
}

void wmr_print_state(WMR *wmr){

    fprintf(stderr, "WMR: HID: %p\n", (void *)wmr->hid);
}

int wmr_close(WMR *wmr){
    hid_return ret;

    ret = hid_close(wmr->hid);
    if (ret != HID_RET_SUCCESS) {
        fprintf(stderr, "hid_close failed with return code %d\n", ret);
        return 1;
    }

    hid_delete_HIDInterface(&wmr->hid);
    wmr->hid = NULL;

    ret = hid_cleanup();
    if (ret != HID_RET_SUCCESS) {
        fprintf(stderr, "hid_cleanup failed with return code %d\n", ret);
        return 1;
    }

    if (wmr->data_fh && wmr->data_fh != stdout) {
        fclose(wmr->data_fh);
        wmr->data_fh = NULL;
    }
    return 0;
}

void wmr_read_packet(WMR *wmr){
    int ret, len;

    ret = hid_interrupt_read(wmr->hid,
                             USB_ENDPOINT_IN + 1,
                             (char*)wmr->buffer,
                             RECV_PACKET_LEN,
                             0);
    if (ret != HID_RET_SUCCESS) {
        fprintf(stderr, "hid_interrupt_read failed with return code %d\n", ret);
        exit(-1);
        return;
    }
    
    len = wmr->buffer[0];
    if (len > 7) len = 7; /* limit */
    wmr->pos = 1;
    wmr->remain = len;
    
    /* dump_packet(wmr->buffer + 1, wmr->remain); */
}

int wmr_read_byte(WMR *wmr){
    while(wmr->remain == 0) {
        wmr_read_packet(wmr);
    }
    wmr->remain--;

    return wmr->buffer[wmr->pos++];
}

int verify_checksum(unsigned char * buf, int len) {
    int i, ret = 0, chk;
    for (i = 0; i < len -2; ++i) {
        ret += buf[i];
    }
    chk = buf[len-2] + (buf[len-1] << 8);

    if (ret != chk) {
        printf("Bad checksum: %d / calc: %d\n", ret, chk);
        return -1;
    }
    return 0;
}

void wmr_log_data(WMR *wmr, char *msg) {
    char outstr[200];
    time_t t;
    struct tm *tmp;
        FILE * out;

    t = time(NULL);
    tmp = gmtime(&t);

    strftime(outstr, sizeof(outstr), "%Y%m%d%H%M%S", tmp);

        out = wmr->data_fh;
        /* check for rolled log or not open */
    if (!access(wmr->data_filename, F_OK) == 0 || wmr->data_fh == NULL) {
            if (wmr->data_fh != NULL) fclose(wmr->data_fh);
                out = wmr->data_fh = fopen(wmr->data_filename, "a+");
                if (wmr->data_fh == NULL) {
                    fprintf(stderr, "ERROR: Couldn't open data log - writing to stderr\n");
                    out = stderr;
                }
        }

    if (gOutput & OUTPUT_FILE)
    {
        fprintf(out, "DATA[%s]:%s\n", outstr, msg);
        fflush(out);
    }
    if (gOutput & OUTPUT_STDOUT)
        printf("DATA[%s]:%s\n", outstr, msg);
}

/****************************
  Data handlers
 ****************************/

void wmr_handle_rain(WMR *wmr, unsigned char *data, int len){
    int sensor, power, rate;
    float hour, day, total;
    int smi, sho, sda, smo, syr;
    char *msg;
    
    sensor = data[2] & 0x0f;
    power = data[2] >> 4;
    rate = data[3];
    
    hour = ((data[5] << 8) + data[4]) * 25.4 / 100.0; /* mm */
    day = ((data[7] << 8) + data[6]) * 25.4 / 100.0; /* mm */
    total = ((data[9] << 8) + data[8]) * 25.4 / 100.0; /* mm */

    smi = data[10];
    sho = data[11];
    sda = data[12];
    smo = data[13];
    syr = data[14] + 2000;

    pthread_mutex_lock(&currentcondition_lock);
        currentcondition->rain_rate = rate;
        currentcondition->rain_hour_total = hour;
        currentcondition->rain_day_total = day;
        currentcondition->rain_all_total = total;
    pthread_mutex_unlock(&currentcondition_lock);

    asprintf(&msg, "type=RAIN,sensor=%d,power=%d,rate=%d,hour_total=%.2f,day_total=%.2f,all_total=%.2f,since=%04d%02d%02d%02d%02d", sensor, power, rate, hour, day, total, syr, smo, sda, sho, smi);
    wmr_log_data(wmr, msg);
    free(msg);
}

void wmr_handle_temp(WMR *wmr, unsigned char *data, int len){
    int sensor, st, smiley, trend, humidity;
    float temp, dewpoint;
    char *trendTxt = "";
    char *msg;

    sensor = data[2] & 0x0f;
    st = data[2] >> 4;
    smiley = st >> 2;
    trend = st & 0x03;

    if (trend <= 2) trendTxt = TRENDS[trend];

    temp = (data[3] + ((data[4] & 0x0f) << 8)) / 10.0;
    if ((data[4] >> 4) == 0x8) temp = -temp;
    
    humidity = data[5];

    dewpoint = (data[6] + ((data[7] & 0x0f) << 8)) / 10.0;
    if ((data[7] >> 4) == 0x8) dewpoint = -dewpoint;

    pthread_mutex_lock(&currentcondition_lock);
        currentcondition->temp[sensor].temp = temp;
        currentcondition->temp[sensor].smile = smiley;
        currentcondition->temp[sensor].humidity = humidity;
        currentcondition->temp[sensor].dewpoint = dewpoint;
        currentcondition->temp[sensor].trend = trendTxt;
    pthread_mutex_unlock(&currentcondition_lock);
    
    asprintf(&msg, "type=TEMP,sensor=%d,smile=%d,trend=%s,temp=%.1f,humidity=%d,dewpoint=%.1f", sensor, smiley, trendTxt, temp, humidity, dewpoint);
    wmr_log_data(wmr, msg);
    free(msg);
}

void wmr_handle_water(WMR *wmr, unsigned char *data, int len){
  int sensor;
  float temp;
  char *msg;

  sensor = data[2] & 0x0f;

  temp = (data[3] + ((data[4] & 0x0f) << 8)) / 10.0;
  if ((data[4] >> 4) == 0x8) temp = -temp;

  pthread_mutex_lock(&currentcondition_lock);
    currentcondition->water[sensor].temp = temp;
  pthread_mutex_unlock(&currentcondition_lock);

  asprintf(&msg, "type=WATER,sensor=%d,temp=%.1f", sensor, temp);
  wmr_log_data(wmr, msg);
  free(msg);
}

void wmr_handle_pressure(WMR *wmr, unsigned char *data, int len){
    int pressure, forecast, alt_pressure, alt_forecast;
    char *msg;

    pressure = data[2] + ((data[3] & 0x0f) << 8);
    forecast = data[3] >> 4;
    alt_pressure = data[4] + ((data[5] & 0x0f) << 8);
    alt_forecast = data[5] >> 4;

    pthread_mutex_lock(&currentcondition_lock);
        currentcondition->pressure = pressure;
        currentcondition->forecast = forecast;
    pthread_mutex_unlock(&currentcondition_lock);

    asprintf(&msg, "type=PRESSURE,pressure=%d,forecast=%d,altpressure=%d,altforecast=%d", pressure, forecast, alt_pressure, alt_forecast);
    wmr_log_data(wmr, msg);
    free(msg);
}

void wmr_handle_uv(WMR *wmr, unsigned char *data, int len){
    char *msg;

    asprintf(&msg, "type=UV");
    wmr_log_data(wmr, msg);
    free(msg);
}

void wmr_handle_wind(WMR *wmr, unsigned char *data, int len){
    char *msg;
    int wind_dir, power, low_speed, high_speed;
    char *wind_str;
    float wind_speed, avg_speed;

    wind_dir = data[2] & 0xf;
    wind_str = WINDIES[wind_dir];
    power = data[2] >> 4;
    
    wind_speed = data[4] / 10.0;

    low_speed = data[5] >> 4;
    high_speed = data[6] << 4;
    avg_speed = (high_speed + low_speed) / 10.0;

    pthread_mutex_lock(&currentcondition_lock);
        currentcondition->wind_speed = wind_speed;
        currentcondition->wind_dir = wind_str;
        currentcondition->wind_avg_speed = avg_speed;
    pthread_mutex_unlock(&currentcondition_lock);

    asprintf(&msg, "type=WIND,power=%d,dir=%s,speed=%.1f,avgspeed=%.1f", power, wind_str, wind_speed, avg_speed);
    wmr_log_data(wmr, msg);
    free(msg);
}

void wmr_handle_clock(WMR *wmr, unsigned char *data, int len){
    int power, powered, battery, rf, level, mi, hr, dy, mo, yr;
    char *msg;

    power = data[0] >> 4;
    powered = power >> 3;
    battery = (power & 0x4) >> 2;
    rf = (power & 0x2) >> 1;
    level = power & 0x1;

    mi = data[4];
    hr = data[5];
    dy = data[6];
    mo = data[7];
    yr = data[8] + 2000;

    asprintf(&msg, "type=CLOCK,at=%04d%02d%02d%02d%02d,powered=%d,battery=%d,rf=%d,level=%d", yr, mo, dy, hr, mi, powered, battery, rf, level);
    wmr_log_data(wmr, msg);
    free(msg);
}

/****************************
 Processing
 ****************************/

void wmr_handle_packet(WMR *wmr, unsigned char *data, int len){
    if (gOutput & OUTPUT_STDOUT)
        dump_packet(data, len);
    
    switch(data[1]) {
    case 0x41:
        wmr_handle_rain(wmr, data, len);
        break;
    case 0x42:
        wmr_handle_temp(wmr, data, len);
        break;
    case 0x44:
        wmr_handle_water(wmr, data, len);
        break;
    case 0x46:
        wmr_handle_pressure(wmr, data, len);
        break;
    case 0x47:
        wmr_handle_uv(wmr, data, len);
        break;
    case 0x48:
        wmr_handle_wind(wmr, data, len);
        break;
    case 0x60:
        wmr_handle_clock(wmr, data, len);
        break;
    }    
}

void wmr_read_data(WMR *wmr){
    int i, j, unk1, type, data_len;
    unsigned char *data;

    /* search for 0xff marker */
    i = wmr_read_byte(wmr);
    while(i != 0xff) {
        i = wmr_read_byte(wmr);
    }

    /* search for not 0xff */
    i = wmr_read_byte(wmr);
    while(i == 0xff) {
        i = wmr_read_byte(wmr);
    }
    unk1 = i;

    /* read data type */
    type = wmr_read_byte(wmr);

    /* read rest of data */
    data_len = 0;
    switch(type) {
    case 0x41:
        data_len = 17;
        break;
    case 0x42:
        data_len = 12;
        break;
    case 0x44:
        data_len = 7;
        break;
    case 0x46:
        data_len = 8;
        break;
    case 0x47:
        data_len = 5;
        break;
    case 0x48:
        data_len = 11;
        break;
    case 0x60:
        data_len = 12;
        break;
    default:
        printf("Unknown packet type: %02x, skipping\n", type);
    }

    if (data_len > 0) {
        data = malloc(data_len);
        data[0] = unk1;
        data[1] = type;
        for (j = 2; j < data_len; ++j) {
            data[j] = wmr_read_byte(wmr);
        }

        if (verify_checksum(data, data_len) == 0) {
            wmr_handle_packet(wmr, data, data_len);
        }

        free(data);
    }

    /* send ack */
    wmr_send_packet_ready(wmr);
}

void wmr_process(WMR *wmr){
    while(true) {
        wmr_read_data(wmr);
    }
}

void cleanup(int sig_num){

    printf("Caught signal, cleaning up\n");

    if (wmr != NULL) {
        wmr_close(wmr);
        wmr = NULL;
    }

    pthread_cancel(sqliteLoggerThread);
    pthread_mutex_destroy(&currentcondition_lock);

    exit(0);
}

/****************************
 Sqlite database
****************************/

sqlite3* openDb(){

    sqlite3 *db;
    char dbFile[20];
    time_t t;
    struct tm *tmp;
    t = time(NULL);
    tmp = gmtime(&t);

    strftime(dbFile, sizeof(dbFile), "weather-%Y-%m.db", tmp);

    if(SQLITE_OK != sqlite3_open(dbFile, &db)) {
        fprintf(stderr,"Error: Can't open database file : %s\n", dbFile);
        return 0;
    }
    return db;
}

void closeDb(sqlite3 *db){
    sqlite3_close(db);
}

bool checkTablesCreated(sqlite3 *db) {

    int i;
    char request[36];
    int nTables = 1;
    char *tables[] = {
        "history"
    };
    char *create[] = {
        "CREATE TABLE history("
          "history INTEGER PRIMARY KEY,"
          "date TEXT,"
          "pressure INTEGER,"
          "forecast INTEGER,"
          "rain_rate INTEGER,"
          "rain_hour_total REAL,"
          "rain_day_total REAL,"
          "rain_all_total REAL,"
          "wind_dir TEXT,"
          "wind_speed REAL,"
          "wind_avg_speed REAL,"
          "uv INTEGER,"
          "temp1 REAL,"
          "temp2 REAL,"
          "temp3 REAL,"
          "temp4 REAL,"
          "temp5 REAL,"
          "smiley1 INTEGER,"
          "smiley2 INTEGER,"
          "smiley3 INTEGER,"
          "smiley4 INTEGER,"
          "smiley5 INTEGER,"
          "humidity1 INTEGER,"
          "humidity2 INTEGER,"
          "humidity3 INTEGER,"
          "humidity4 INTEGER,"
          "humidity5 INTEGER,"
          "dewpoint1 REAL,"
          "dewpoint2 REAL,"
          "dewpoint3 REAL,"
          "dewpoint4 REAL,"
          "dewpoint5 REAL,"
          "trend1 TEXT,"
          "trend2 TEXT,"
          "trend3 TEXT,"
          "trend4 TEXT,"
          "trend5 TEXT,"
          "waterTemp1 REAL,"
          "waterTemp2 REAL,"
          "waterTemp3 REAL,"
          "waterTemp4 REAL,"
          "waterTemp5 REAL);"
    };
    
    for( i = 0; i < nTables; i++) {
        sprintf(request, "SELECT COUNT(*) FROM %s;", tables[i]);
        if(SQLITE_OK != sqlite3_exec(db, request, 0, 0, 0)) {
            fprintf(stderr,"The table %s no existe\n",tables[i]);
            if(SQLITE_OK != sqlite3_exec(db, create[i], 0, 0, 0)) {
               fprintf(stderr,"Error, can't create table : %s\n",tables[i]);
               return false;
            }
        }
    }
    return true;
}

void* sqliteLoggerThreadFct(void *args){

    while(1){

        sqlite3 *db;
        char request[2000];
        char currenttime[20];
        time_t t;
        struct tm *tmp;
    
        /* Record every RECORD_HISTORY */

        sleep(RECORD_HISTORY);

        if ((db = openDb()) == 0){
            exit(-1);
        }
        checkTablesCreated(db);
    
        /* Check if the database should be changed */

        t = time(NULL);
        tmp = gmtime(&t);

        strftime(currenttime, sizeof(currenttime), "%Y%m%d%H%M%S", tmp);

        pthread_mutex_lock(&currentcondition_lock);

            sprintf(request, "insert into history (date,pressure,forecast,rain_rate,rain_hour_total,rain_day_total,rain_all_total,wind_dir,wind_speed,wind_avg_speed,uv,temp1,temp2,temp3,temp4,temp5,smiley1,smiley2,smiley3,smiley4,smiley5,humidity1 ,humidity2 ,humidity3 ,humidity4 ,humidity5 ,dewpoint1 ,dewpoint2 ,dewpoint3 ,dewpoint4 ,dewpoint5 ,trend1,trend2,trend3,trend4,trend5,waterTemp1,waterTemp2,waterTemp3,waterTemp4,waterTemp5) values('%s',%d,%d,%d,%.2f,%.2f,%.2f,'%s',%.2f,%.2f,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.2f,%.2f,%.2f,%.2f,%.2f,'%s','%s','%s','%s','%s',%.2f,%.2f,%.2f,%.2f,%.2f);", 
                currenttime, currentcondition->pressure, currentcondition->forecast,
                currentcondition->rain_rate, currentcondition->rain_hour_total, currentcondition->rain_day_total,currentcondition->rain_all_total,
                currentcondition->wind_dir, currentcondition->wind_speed, currentcondition->wind_avg_speed,
                currentcondition->uv,
                currentcondition->temp[0].temp,currentcondition->temp[1].temp,currentcondition->temp[2].temp,currentcondition->temp[3].temp,currentcondition->temp[4].temp,
                currentcondition->temp[0].smile,currentcondition->temp[1].smile,currentcondition->temp[2].smile,currentcondition->temp[3].smile,currentcondition->temp[4].smile,
                currentcondition->temp[0].humidity,currentcondition->temp[1].humidity,currentcondition->temp[2].humidity,currentcondition->temp[3].humidity,currentcondition->temp[4].humidity,
                currentcondition->temp[0].dewpoint,currentcondition->temp[1].dewpoint,currentcondition->temp[2].dewpoint,currentcondition->temp[3].dewpoint,currentcondition->temp[4].dewpoint,
                currentcondition->temp[0].trend,currentcondition->temp[1].trend,currentcondition->temp[2].trend,currentcondition->temp[3].trend,currentcondition->temp[4].trend,
                currentcondition->water[0].temp,currentcondition->water[1].temp,currentcondition->water[2].temp,currentcondition->water[3].temp,currentcondition->water[4].temp);

        pthread_mutex_unlock(&currentcondition_lock);


        if(SQLITE_OK != sqlite3_exec(db, request, 0, 0, 0)) {
            fprintf(stderr, "[%s] Can't write to database\n",currenttime);
            fprintf(stderr, "    Request : %s\n", request );
            fprintf(stderr, "    Error : %s\n",sqlite3_errmsg(db));
        }
        else {
            printf("[%s] Write to database ok\n",currenttime);
        }

        /* Close the database */

        closeDb(db);
    }
}

/****************************
 Main
 ****************************/

int main(int argc, char* argv[]){

    int ret;
    int c;
    int i;

    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);

    /* Parse the command line parameters */
    while ((c = getopt(argc, argv, "hsfb")) != -1)
    {
        switch (c)
        {
        case 'h':
            fprintf(stderr, "Options:\n\t-s: output to sdtout only\n\t-f: output to file only\n\t-b: output to both [default]\n");
            return 1;
        case 's': 
            gOutput = OUTPUT_STDOUT;
            break;
        case 'f':
            gOutput = OUTPUT_FILE;
            break;
        case 'b':
            gOutput = OUTPUT_BOTH;
            break;
        case '?':
            if (isprint(optopt))
                fprintf(stderr, "Unknown option `-%c'.\n", optopt);
            return 1;
        default:
            abort();
        }
    }

    /* CURRENT CONDITION INITIALISATION */

    currentcondition = malloc(sizeof(CURRENTCONDITION) + 10 * sizeof(char));
    
    currentcondition->pressure = -1;
    currentcondition->forecast = -1;
    currentcondition->rain_rate = -1;
    currentcondition->rain_hour_total = -1.0;
    currentcondition->rain_day_total = -1.0;
    currentcondition->rain_all_total = -1.0;
    currentcondition->wind_dir = "";
    currentcondition->wind_speed = -1.0;
    currentcondition->wind_avg_speed = -1.0;
    currentcondition->uv = -1;
    
    for (i=0; i < 5; i++){

        currentcondition->temp[i].temp = -1.0;
        currentcondition->temp[i].smile = -1;
        currentcondition->temp[i].humidity = -1;
        currentcondition->temp[i].dewpoint = -1.0;
        currentcondition->temp[i].trend = "";    

        currentcondition->water[i].temp = -1.0;
    }

    /* LOGGER THREAD INIT */

    pthread_create(&sqliteLoggerThread, NULL, &sqliteLoggerThreadFct, NULL);
    if (pthread_mutex_init(&currentcondition_lock, NULL) != 0)
    {
        fprintf(stderr,"\nMutex init failed\n");
        return 1;
    }

        /* TODO : Move to close function */
        /* pthread_cancel(sqliteLoggerThread); */
        /* pthread_mutex_destroy(&lock); */

    /* WMR INIT */

    wmr = wmr_new();
    if (wmr == NULL) {
      fprintf(stderr, "wmr_new failed\n");
      exit(-1);
    }
    printf("Opening WMR100...\n");
    ret = wmr_init(wmr);
    if (ret != 0) {
      fprintf(stderr, "Failed to init USB device, exiting.\n");
      exit(-1);
    }

    printf("Found on USB: %s\n", wmr->hid->id);
    wmr_print_state(wmr);
    wmr_process(wmr);
    wmr_close(wmr);
    printf("Closed WMR100\n");
  
    return 0;
}
