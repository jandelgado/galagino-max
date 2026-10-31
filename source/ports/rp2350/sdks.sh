#!/usr/bin/env bash
# usage: sdks.sh   clones the pinned RP2350 SDKs into <repo>/sdks (git-ignored),
# skipping those already there. build.sh looks there unless PICO_SDK_PATH,
# FREERTOS_KERNEL_PATH or BLUEPAD32_PATH point elsewhere.
set -e
dir=${SDKS_DIR:-$(cd "$(dirname "$0")/../../.." && pwd)/sdks}
mkdir -p "$dir"
cd "$dir"

# submodules: USB stdio, CYW43 radio, BTstack, lwIP (remote mode); not
# shallow, btstack's pinned commit is no branch tip
if [ ! -d pico-sdk ]; then
  git clone -b 2.2.0 --depth 1 https://github.com/raspberrypi/pico-sdk.git
  git -C pico-sdk submodule update --init lib/tinyusb lib/cyw43-driver lib/btstack lib/lwip
fi

# the RP2350 port lives in the Community-Supported-Ports submodule
if [ ! -d FreeRTOS-Kernel ]; then
  git clone -b V11.3.2 --depth 1 https://github.com/FreeRTOS/FreeRTOS-Kernel.git
  git -C FreeRTOS-Kernel submodule update --init --depth 1 portable/ThirdParty/Community-Supported-Ports
fi

# Bluetooth pad (Pico 2 W)
if [ ! -d bluepad32 ]; then
  git clone -b 4.2.0 --depth 1 https://github.com/ricardoquesada/bluepad32.git
fi
echo "SDKs in $dir"
