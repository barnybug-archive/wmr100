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
#include <zmq.h>
#include <sys/time.h>
#include <assert.h>

#define WMR100_VENDOR_ID  0x0fde
#define WMR100_PRODUCT_ID 0xca01

/****************************
 Variables definition
****************************/

/* Logging output */

bool gOutputStdout = false;
bool gOutputFile = false;
char *gOutputZmq = NULL;

/* Constants */

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
char *const TRENDS[] = { "0", "1", "-1" };
char *const WINDIES[] = { "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NWN" };

/* WMR */

typedef struct _WMR {
    int pos;
    int remain;
    unsigned char* buffer;
    HIDInterface *hid;
    FILE *data_fh;
    char *data_filename;
    void *zmq_ctx;
    void *zmq_sock;
} WMR;

WMR *wmr = NULL;

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

void wmr_output_file(WMR *wmr, char *msg) {
    FILE * out = wmr->data_fh;

    /* check for rolled log or not open */
    if (!access(wmr->data_filename, F_OK) == 0 || wmr->data_fh == NULL) {
        if (wmr->data_fh != NULL)
            fclose(wmr->data_fh);
        out = wmr->data_fh = fopen(wmr->data_filename, "a+");
        if (wmr->data_fh == NULL) {
            fprintf(stderr, "ERROR: Couldn't open data log - writing to stderr\n");
            out = stderr;
        }
    }

    fprintf(out, "%s\n", msg);
    fflush(out);
}

void wmr_output_stdout(WMR *wmr, char *msg) {
    printf("%s\n", msg);
}

void wmr_output_zmq(WMR *wmr, char *topic, char *msg) {
    zmq_msg_t zmsg;
    int len = strlen(topic) + 1 + strlen(msg);
    char *data;

    zmq_msg_init_size(&zmsg, len);

    /* message format is: topic\0json, for pubsub subscription matching */
    data = (char *)zmq_msg_data(&zmsg);
    strcpy(data, topic);
    data += strlen(topic);
    data[0] = '\0';
    data += 1;
    strcpy(data, msg);
    zmq_send(wmr->zmq_sock, &zmsg, 0);
    zmq_msg_close(&zmsg);
}

void wmr_log_data(WMR *wmr, char *topic, char *msg) {
    char timestamp[200];
    char *buf;
    struct tm *tmp;
    struct timeval tv;

    gettimeofday(&tv, NULL);
    tmp = gmtime(&tv.tv_sec);

    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tmp);
    asprintf(&buf,
        "{\"topic\": \"%s\", "
        "\"timestamp\": \"%s.%06d\", "
        "%s}",
        topic,
        timestamp,
        (int)tv.tv_usec,
        msg);

    if (gOutputFile) {
        wmr_output_file(wmr, buf);
    }
    if (gOutputStdout) {
        wmr_output_stdout(wmr, buf);
    }
    if (gOutputZmq) {
        wmr_output_zmq(wmr, topic, buf);
    }

    free(buf);
}

/****************************
  Data handlers
 ****************************/

void wmr_handle_rain(WMR *wmr, unsigned char *data, int len) {
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

    asprintf(&msg,
             "\"sensor\": %d, "
             "\"power\": %d, "
             "\"rate\": %d, "
             "\"hour_total\": %.2f, "
             "\"day_total\": %.2f, "
             "\"all_total\": %.2f, "
             "\"since\": \"%04d%02d%02d%02d%02d\", "
             "\"source\": \"wmr100.%d\"",
             sensor, power, rate, hour, day, total, syr, smo, sda, sho, smi, sensor);
    wmr_log_data(wmr, "rain", msg);
    free(msg);
}

void wmr_handle_temp(WMR *wmr, unsigned char *data, int len){
    int sensor, st, smiley, trend, humidity;
    float temp, dewpoint;
    char *msg;

    sensor = data[2] & 0x0f;
    st = data[2] >> 4;
    smiley = st >> 2;
    trend = st & 0x03;
    trend -= 1;

    temp = (data[3] + ((data[4] & 0x0f) << 8)) / 10.0;
    if ((data[4] >> 4) == 0x8) temp = -temp;
    
    humidity = data[5];

    dewpoint = (data[6] + ((data[7] & 0x0f) << 8)) / 10.0;
    if ((data[7] >> 4) == 0x8) dewpoint = -dewpoint;

    asprintf(&msg,
             "\"sensor\": %d, "
             "\"smile\": %d, "
             "\"trend\": %d, "
             "\"temp\": %.1f, "
             "\"humidity\": %d, "
             "\"dewpoint\": %.1f, "
             "\"source\": \"wmr100.%d\"",
             sensor, smiley, trend, temp, humidity, dewpoint, sensor);
    wmr_log_data(wmr, "temp", msg);
    free(msg);
}

void wmr_handle_water(WMR *wmr, unsigned char *data, int len){
    int sensor;
    float temp;
    char *msg;

    sensor = data[2] & 0x0f;

    temp = (data[3] + ((data[4] & 0x0f) << 8)) / 10.0;
    if ((data[4] >> 4) == 0x8)
        temp = -temp;

    asprintf(&msg,
             "\"sensor\": %d, "
             "\"temp\": %.1f, "
             "\"source\": \"wmr100\"",
             sensor, temp);
    wmr_log_data(wmr, "water", msg);
    free(msg);
}

void wmr_handle_pressure(WMR *wmr, unsigned char *data, int len){
    int pressure, forecast, alt_pressure, alt_forecast;
    char *msg;

    pressure = data[2] + ((data[3] & 0x0f) << 8);
    forecast = data[3] >> 4;
    alt_pressure = data[4] + ((data[5] & 0x0f) << 8);
    alt_forecast = data[5] >> 4;

    asprintf(&msg,
             "\"pressure\": %d, "
             "\"forecast\": %d, "
             "\"altpressure\": %d, "
             "\"altforecast\": %d, "
             "\"source\": \"wmr100\"",
             pressure, forecast, alt_pressure, alt_forecast);
    wmr_log_data(wmr, "pressure", msg);
    free(msg);
}

void wmr_handle_uv(WMR *wmr, unsigned char *data, int len){
    char *msg;

    asprintf(&msg, "\"todo\": 1");
    wmr_log_data(wmr, "uv", msg);
    free(msg);
}

void wmr_handle_wind(WMR *wmr, unsigned char *data, int len){
    char *msg;
    int wind_dir, power, low_speed, high_speed;
    float wind_speed, avg_speed;

    wind_dir = data[2] & 0xf;
    power = data[2] >> 4;
    
    wind_speed = data[4] / 10.0;

    low_speed = data[5] >> 4;
    high_speed = data[6] << 4;
    avg_speed = (high_speed + low_speed) / 10.0;

    asprintf(&msg,
             "\"power\": %d, "
             "\"dir\": %d, "
             "\"speed\": %.1f, "
             "\"avgspeed\": %.1f, "
             "\"source\": \"wmr100\"",
             power, wind_dir, wind_speed, avg_speed);
    wmr_log_data(wmr, "wind", msg);
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

    asprintf(&msg,
             "\"at\": \"%04d%02d%02d%02d%02d\", "
             "\"powered\": %d, "
             "\"battery\": %d, "
             "\"rf\": %d, "
             "\"level\": %d, "
             "\"source\": \"wmr100\"",
             yr, mo, dy, hr, mi, powered, battery, rf, level);
    wmr_log_data(wmr, "clock", msg);
    free(msg);
}

/****************************
 Processing
 ****************************/

void wmr_handle_packet(WMR *wmr, unsigned char *data, int len) {
    if (gOutputStdout)
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

void wmr_read_data(WMR *wmr) {
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

void wmr_process(WMR *wmr) {
    while(true) {
        wmr_read_data(wmr);
    }
}

void cleanup(int sig_num) {
    printf("Caught signal, cleaning up\n");

    if (gOutputZmq) {
        zmq_close(wmr->zmq_sock);
        zmq_term(wmr->zmq_ctx);
    }
    
    if (wmr != NULL) {
        wmr_close(wmr);
        wmr = NULL;
    }

    exit(0);
}

/****************************
 Main
 ****************************/

int init_output_zmq(WMR *wmr) {
    wmr->zmq_ctx = zmq_init(1);
    assert(wmr->zmq_ctx != NULL);
    wmr->zmq_sock = zmq_socket(wmr->zmq_ctx, ZMQ_PUB);
    assert(wmr->zmq_sock != NULL);
    assert(gOutputZmq);
    zmq_connect(wmr->zmq_sock, gOutputZmq);

    return 0;
}

int main(int argc, char* argv[]) {
    int ret;
    int c;

    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);

    /* Parse the command line parameters */
    while ((c = getopt(argc, argv, "hsfdz:")) != -1)
    {
        switch (c)
        {
        case 'h':
            fprintf(stderr, "Options:\n"
                "\t-s: output to sdtout only\n"
                "\t-f: output to file only\n"
                "\t-z endpoint (eg. tcp://*:8790): output to zmq endpoint\n");
            return 1;
        case 's': 
            gOutputStdout = true;
            break;
        case 'f':
            gOutputFile = true;
            break;
        case 'z':
            gOutputZmq = optarg; /* zeromq endpoint */
            break;
        case '?':
            if (isprint(optopt))
                fprintf(stderr, "Unknown option `-%c'.\n", optopt);
            return 1;
        default:
            abort();
        }
    }

    if (!(gOutputStdout || gOutputFile || gOutputZmq)) {
        /* set default outputs */
        gOutputStdout = true;
        gOutputFile = true;        
    }

    /* Initialize WMR */
    wmr = wmr_new();
    if (wmr == NULL) {
        perror("wmr_new failed");
        exit(1);
    }

    fprintf(stderr, "Writing data to:\n");
    if (gOutputStdout)
        fprintf(stderr, "- Stdout\n");
    if (gOutputFile)
        fprintf(stderr, "- File\n");
    if (gOutputZmq) {
        fprintf(stderr, "- Zmq\n");
        init_output_zmq(wmr);
    }

    printf("Opening WMR100...\n");
    ret = wmr_init(wmr);
    if (ret != 0) {
        perror("Failed to init USB device, exiting.");
        exit(1);
    }

    printf("Found on USB: %s\n", wmr->hid->id);
    wmr_print_state(wmr);
    wmr_process(wmr);
    wmr_close(wmr);
    printf("Closed WMR100\n");
  
    return 0;
}
