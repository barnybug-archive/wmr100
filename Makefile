CPPFLAGS += `pkg-config libhid --cflags`
LDFLAGS += `pkg-config libhid --libs`

wmr100: wmr100.c
