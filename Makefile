CPPFLAGS += `pkg-config libhid --cflags` -pedantic
LDFLAGS += `pkg-config libhid --libs`

wmr100: wmr100.c

clean:
	-rm wmr100