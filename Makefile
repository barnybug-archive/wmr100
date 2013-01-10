CFLAGS += `pkg-config libhid --cflags` -pedantic -Wall -D_GNU_SOURCE -Wno-overlength-strings
LIBS += `pkg-config libhid --libs` -lsqlite3 -lpthread

wmr100: wmr100.c
	cc ${CFLAGS} -o wmr100 wmr100.c ${LIBS}

clean:
	-rm wmr100

setup_osx:
	sudo cp -r osx/wmr100.kext /System/Library/Extensions/wmr100.kext
	sudo kextload -vt /System/Library/Extensions/wmr100.kext
	sudo touch /System/Library/Extensions

unsetup_osx:
	sudo rm -r -i /System/Library/Extensions/wmr100.kext
	sudo touch /System/Library/Extensions
	echo Please reboot for changes to take effect.