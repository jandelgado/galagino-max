.PHONY: phony

DEVICE_BASE := /dev/ttyUSB
DEVICE_BASE := /dev/ttyACM
DEFAULT_ENV := cyd-2432s028r
DEFAULT_ENV := cyd-esp32-s3-display-usb-up

ifneq (,$(wildcard /dev/ttyUSB*))
  DEVICE_BASE := /dev/ttyUSB
endif
ifneq (,$(wildcard /dev/ttyACM*))
  DEVICE_BASE := /dev/ttyACM
endif
DEVICE ?= 0
DEVICE_PATH := $(DEVICE_BASE)$(DEVICE)

ENV ?= $(DEFAULT_ENV)

ifeq ($(V),)
        VERBOSE :=
else
        VERBOSE := --verbose --jobs=1
endif

config: phony
	pio project config

build: phony
	pio run $(VERBOSE) -e $(ENV)

monitor: phony
	pio device monitor -e $(ENV) --port $(DEVICE_PATH)

upload: phony
	pio run $(VERBOSE) -e $(ENV) -t upload -t monitor

upload-only: phony
	pio run $(VERBOSE) -e $(ENV) -t upload

release_azDeliveryDevKitV4: phony
	$(MAKE) upload ENV=$@

cyd-2432s028r: phony
	$(MAKE) upload ENV=$@

cyd-fnk0103f: phony
	$(MAKE) upload ENV=$@

cyd-fnk0103b: phony
	$(MAKE) upload ENV=$@

# ST7789 - black
cyd-fnk0103b-usb-up: phony
	$(MAKE) upload ENV=$@

# ILI9341 - transparent
cyd-fnk0103f-usb-up: phony
	$(MAKE) upload ENV=$@

# ILI9341 - silk white
cyd-esp32-s3-display-usb-up: phony
	$(MAKE) upload ENV=$@

release_cheapYellowDisplay: phony
	$(MAKE) upload ENV=$@

image-info: phony
	esptool --chip esp32s3 --port $(DEVICE_PATH) image-info .pio/build/cyd-esp32-s3-display-usb-up/GALAGINO_*_release.bin

verify-flash: phony
	esptool --chip esp32s3 --port $(DEVICE_PATH) \
	  verify-flash --diff 0x10000 .pio/build/cyd-esp32-s3-display-usb-up/GALAGINO_*_release.bin

erase-flash: phony
	esptool --chip esp32s3 --port $(DEVICE_PATH) erase-flash

flash-id: phony
	esptool --port $(DEVICE_PATH) flash-id

phony:

