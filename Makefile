CPPFLAGS += `pkg-config libhid --cflags` -pedantic
LDFLAGS += `pkg-config libhid --libs`

wmr100: wmr100.c

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