## Usage (from source/, env names: make envs, see platformio.ini):
##   make <env>                 build, upload, monitor, e.g. make azdelivery-local
##   make build ENV=<env>       build only
##   make upload-only ENV=<env> build, upload
##   make monitor ENV=<env>     serial monitor
##   make config                resolved platformio.ini
##   make image-info|verify-flash|erase-flash|flash-id ENV=<env>   esptool
## Options: DEVICE=1 (/dev/ttyUSB1 or /dev/ttyACM1), V=1 (verbose build)
## Without ENV: DEFAULT_ENV in Makefile.

.PHONY: phony

help: phony
	@sed -n 's/^## \{0,1\}//p' $(MAKEFILE_LIST)

DEFAULT_ENV := fnk0104-full-usbup

DEVICE_BASE := /dev/ttyACM
ifneq (,$(wildcard /dev/ttyUSB*))
  DEVICE_BASE := /dev/ttyUSB
endif
ifneq (,$(wildcard /dev/ttyACM*))
  DEVICE_BASE := /dev/ttyACM
endif
DEVICE ?= 0
DEVICE_PATH := $(DEVICE_BASE)$(DEVICE)

ENV ?= $(DEFAULT_ENV)
BIN = .pio/build/$(ENV)/GALAGINO_*_release.bin

# every device env of platformio.ini
ENVS := $(filter-out native,$(shell sed -n 's/^\[env:\(.*\)\]/\1/p' platformio.ini))

ifeq ($(V),)
        VERBOSE :=
else
        VERBOSE := --verbose --jobs=1
endif

config: phony
	pio project config

envs: phony
	@echo $(ENVS)

build: phony
	pio run $(VERBOSE) -e $(ENV)

monitor: phony
	pio device monitor -e $(ENV) --port $(DEVICE_PATH)

upload: phony
	pio run $(VERBOSE) -e $(ENV) -t upload -t monitor

upload-only: phony
	pio run $(VERBOSE) -e $(ENV) -t upload

# make <env>: upload + monitor that env
$(ENVS): phony
	$(MAKE) upload ENV=$@

image-info: phony
	esptool image-info $(BIN)

verify-flash: phony
	esptool --port $(DEVICE_PATH) verify-flash --diff 0x10000 $(BIN)

erase-flash: phony
	esptool --port $(DEVICE_PATH) erase-flash

flash-id: phony
	esptool --port $(DEVICE_PATH) flash-id

phony:
