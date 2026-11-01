## Usage (from source/, env names: make envs, see [envs] of firmware.toml):
##   make <env>                 build, upload, monitor, e.g. make azdelivery-local
##   make build ENV=<env>       build only
##   make upload-only ENV=<env> build, upload
## RP2350 envs (e.g. pico2w_dvi-local_bt) build with ports/rp2350/build.sh and
## upload with picotool; GAMES=a,b and DEFINES="A B" work for both platforms.
##   make sdks                  clone the RP2350 SDKs into <repo>/sdks (once)
##   make monitor ENV=<env>     serial monitor
##   make config                resolved platformio.ini
##   make image-info|verify-flash|erase-flash|flash-id ENV=<env>   esptool
## Options: DEVICE=1 (/dev/ttyUSB1 or /dev/ttyACM1), V=1 (verbose build)
## Without ENV: DEFAULT_ENV in Makefile.

.PHONY: phony

help: phony
	@sed -n 's/^## \{0,1\}//p' $(MAKEFILE_LIST)

DEFAULT_ENV := fnk0104-full

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
# PlatformIO and esptool, pinned, run by uv
PIO ?= ./tools/pio
ESPTOOL ?= uvx --from esptool==5.2.0 esptool
PICOTOOL ?= picotool
BIN = .pio/build/$(ENV)/GALAGINOMAX_*_release.bin

# every device env of platformio.ini
ENVS := $(filter-out native,$(shell sed -n 's/^\[env:\(.*\)\]/\1/p' platformio.ini))
# every rp2350 env of firmware.toml; build.sh names its build dir after GAMES
RP_ENVS := $(shell sed -n 's/^\([^ ]*\) = { platform = "rp2350".*/\1/p' firmware.toml)
comma := ,
# image name: [firmware] of firmware.toml
FW_NAME := $(shell sed -n 's/^name = "\(.*\)"/\1/p' firmware.toml)
FW_VERSION := $(shell sed -n 's/^version = "\(.*\)"/\1/p' firmware.toml)
RP_UF2 = ports/rp2350/build/$(ENV)$(if $(GAMES),-$(subst $(comma),-,$(GAMES)))/$(FW_NAME)_$(FW_VERSION).uf2

ifeq ($(V),)
        VERBOSE :=
else
        VERBOSE := --verbose --jobs=1
endif

config: phony
	$(PIO) project config

sdks: phony
	ports/rp2350/sdks.sh

envs: phony
	@echo $(ENVS) $(RP_ENVS)

ifneq (,$(filter $(ENV),$(RP_ENVS)))
build: phony
	ports/rp2350/build.sh $(ENV)

# -f reboots a running Pico into BOOTSEL first
upload-only: build
	$(PICOTOOL) load -f -x $(RP_UF2)

monitor: phony
	$(PIO) device monitor --port $(DEVICE_PATH)

upload: upload-only
	$(MAKE) monitor ENV=$(ENV)
else
build: phony
	$(PIO) run $(VERBOSE) -e $(ENV)

monitor: phony
	$(PIO) device monitor -e $(ENV) --port $(DEVICE_PATH)

upload: phony
	$(PIO) run $(VERBOSE) -e $(ENV) -t upload -t monitor

upload-only: phony
	$(PIO) run $(VERBOSE) -e $(ENV) -t upload
endif

# make <env>: upload + monitor that env
$(ENVS) $(RP_ENVS): phony
	$(MAKE) upload ENV=$@

image-info: phony
	$(ESPTOOL) image-info $(BIN)

verify-flash: phony
	$(ESPTOOL) --port $(DEVICE_PATH) verify-flash --diff 0x10000 $(BIN)

erase-flash: phony
	$(ESPTOOL) --port $(DEVICE_PATH) erase-flash

flash-id: phony
	$(ESPTOOL) --port $(DEVICE_PATH) flash-id

phony:
