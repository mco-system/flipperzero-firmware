$(info +-------------------------------------------------+)
$(info |                                                 |)
$(info |      Hello, this is Flipper team speaking!      |)
$(info |                                                 |)
$(info |       We've migrated to new build system        |)
$(info |          It's nice and based on scons           |)
$(info |                                                 |)
$(info |      Crash course:                              |)
$(info |                                                 |)
$(info |        `./fbt`                                  |)
$(info |        `./fbt flash`                            |)
$(info |        `./fbt debug`                            |)
$(info |                                                 |)
$(info |      More details in documentation/fbt.md       |)
$(info |                                                 |)
$(info |      Also Please leave your feedback here:      |)
$(info |           https://flipp.dev/4RDu                |)
$(info |                      or                         |)
$(info |           https://flipp.dev/2XM8                |)
$(info |                                                 |)
$(info +-------------------------------------------------+)

.PHONY: build flash clean distclean all

distclean:
	-rm -Rf ./build
	-rm -Rf ./dist
	-rm -Rf ./toolchain

clean:
	-rm -Rf ./build
	-rm -Rf ./dist
	. ./scripts/toolchain/fbtenv.sh; python3 ./lib/scons/scripts/scons.py --clean
	-find ./ -type d -name "*__pycache__" -exec rm -Rf {} \;
	-rm ./.sconsign.dblite

build:
	./fbt COMPACT=1 DEBUG=0 VERBOSE=1 updater_package copro_dist

flash:
	./fbt COMPACT=1 DEBUG=0 VERBOSE=1 flash_usb_full

all: | distclean clean build flash

rebuild: | clean build
