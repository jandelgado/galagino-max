# HAL contract

Every name below exists in each backend directory (`esp/`, `host/`, `rp2350/`) with this
signature. Portable code includes only `hal/hal.h` (and `hal_attr.h`). Backend selection:
`HAL_ESP`, `HAL_RP2350` or `HAL_HOST`, defined by the build. All names are in `namespace hal`.

## Time (`time.h`, inline on ESP and host)
- `uint32_t millis()`, `uint32_t micros()`: wrap at 2^32, compare with unsigned differences.
- RP2350: out of line in rp2350/time.cpp, a function call each. Pico SDK headers must stay
  out of the HAL headers (their register macros collide with names in the machines).

## GPIO (`gpio.h`, inline)
- `void pinOutput(int pin, bool level)`
- `void pinInputPullup(int pin)`
- `void pinWrite(int pin, bool level)`
- `bool pinRead(int pin)`

## Tasks and sync (`rtos.h`)
- `VIDEO_CORE`, `EMULATION_CORE`: core of the main (video/audio) task and of the emulation task.
- `typedef ... TaskHandle`
- `TaskHandle taskSpawn(void (*fn)(void*), void *arg, const char *name, uint32_t stackBytes,
  uint32_t priority, int core)`: NULL on failure.
- `void taskExit()`: deletes the calling task.
- `void yield()`: lets a ready task of the same priority on this core run (busy tasks call it).
- `int coreId()` (host: always `EMULATION_CORE`), `void sleepMs(uint32_t ms)`: sleep yields for any ms > 0.
- `class Signal { void give(); bool take(uint32_t timeoutMs); void clear(); }`: latched
  event, several gives before a take count once. `WAIT_FOREVER` as timeout.
- `class CriticalSection`: RAII, one process-wide lock, safe across cores, keep sections short.

## Watchdog (`watchdog.h`)
- `void watchdogWatch(TaskHandle, uint32_t seconds)`: panics without feeds; call once per task.
- `void watchdogFeed()`, `void watchdogUnwatch(TaskHandle)`
- `void idleWatchdogEnable(int core, bool on)`

## LEDs (`led.h`)
- `class LedStrip { static void init(int pin, uint32_t count); static void show(const uint8_t *rgb,
  uint32_t count); }`: 3 bytes (r, g, b) per LED; the backend applies `LED_BRIGHTNESS`. Absent
  strip (no `LED_PIN`): both calls do nothing.
- `led_color.h` (not backend specific): `CRGB`, `fill_solid`, `nblend`, FastLED-compatible subset.

## Audio (`audio.h`, `audio_config.h`)
- `struct AudioConfig { uint32_t rate; uint32_t chunk; uint32_t buffers; }` (24000, 64, 4).
- `class AudioOut { static void init(const AudioConfig&); static bool write(const int16_t*);
  static void writeBlocking(const int16_t*); static void flushSilence(); }`: signed 16 bit mono
  after volume. `init` ramps the output to idle level without a click; `write` takes one chunk if
  a buffer is free, else returns false (all or nothing); `flushSilence` fills every buffer.
- Backend by define (selected in hal/audio.h, set by the board profile; build defines, not API):
  HAL_AUDIO_ESP_DAC, HAL_AUDIO_ES8311 (I2S stream; Audio::init sets up the codec over I2c first),
  HAL_AUDIO_NULL, HAL_AUDIO_DVI (HDMI audio in the DVI stream, scanout clocked; DVI boards'
  default, HAL_AUDIO_NULL gives plain silent DVI). Combinations: config_check.h.

## Video (`video.h`, not in `hal.h`)
`hal::Video` is the backend class (`SpiTftVideo`, `DviHstxVideo`, `NullVideo`). Strips are
240 x 8 RGB565 pixels in the order the machines render them (SPI byte order).
- `void begin()`, `void setViewport(uint16_t width)` (224 or 240, every frame),
  `void flip(char flipY, char flipX)`, `void flipReset(char flipY, char flipX)`
- `void setHalfRate(bool)`: the machine's preference, called every frame before `screenParts()`.
- `uint8_t screenParts() const`: 1, or 2 when a screen takes two emulation frames.
- `void submit(const uint16_t *strip, uint32_t pixels)`: may block. A screen is 36 strips, then
  `endFrame()`: DVI drops strips past 36 until it (the remote loader skips it: ESP32 only).
- `void endFrame(uint8_t part, uint32_t frameStartUs)`: after the last strip of a part; returns
  when the next part may start (backend pacing: deadline sleep, or scanout position).
- `CANVAS_W`, `CANVAS_H` (compile-time): game pixels the output can show (SPI TFT and null
  240 x 320; DVI 640 x 480 at 1x, 320 x 320 at 2x).
- `uint8_t stripIndex(uint8_t i) const`: strip to render as the i-th of a screen. Identity where
  the panel flips (SPI TFT); DVI scans top to bottom, so a flipY machine renders strip `35 - i`.

## System (`sys.h`)
- `struct SysInfo { chip, revision, cores, cpuMhz, flashBytes, sdkVersion }`, `void sysInfo(SysInfo&)`
- `struct MemInfo { free, largest, minFree }`, `MemInfo memInfo()` (internal RAM),
  `MemInfo memInfoData()` (byte addressable data RAM), `void memDump()`
- `void *allocSpare(uint32_t bytes)`: permanent buffer outside the main heap's largest region
  where the platform has one (ESP32: D/IRAM pool), `malloc` fallback, free with `free()`;
  `uint32_t random32()` (hardware random)
- `void consoleInit()`, `void printPlatformInfo()`, `bool imageVerify(bool &supported)`,
  `bool resetWasPanic()`

## I2C (`i2c.h`)
- `class I2c { static bool init(int port, int sda, int scl, uint32_t hz); static uint32_t
  clockHz(int port); static bool probe(int port, uint8_t addr); static bool write(int port,
  uint8_t addr, const uint8_t*, uint32_t len); static uint32_t read(int port, uint8_t addr,
  uint8_t*, uint32_t len) /* bytes read */; static bool writeRead(...); }`
- `probe`: RP2350 reads one byte (its I2C block has no address-only transfer); a device with a
  read pointer advances it.

## Gamepad (`gamepad.h`, not in `hal.h`; `HAL_GAMEPAD_BT` builds, host and RP2350 only)
`GamepadState` (connected, dpad bits, stick x/y -512..511 with y negative
up, button bits) is defined in `gamepad.h` itself. Player 1's pad, further pads ignored.
- `class Gamepad { static void begin(); static void read(GamepadState &out); }`: `begin()` once
  at boot (radio up, pairing open); `read()` from any core, never blocks, a consistent copy.
- Host: host::setGamepad(state) scripts the state (tests).

## Flash region (`flash_region.h`, not in `hal.h`; ESP and host only)
`class FlashRegion`, erased bytes read 0xFF, a write only clears bits. Host: RAM fake, extra
size argument.
- `explicit FlashRegion(const char *label)`, `uint32_t size() const` (0: no region)
- `bool erase(uint32_t offset, uint32_t length)` (4 KB aligned)
- `bool write(uint32_t offset, const uint8_t *data, uint32_t length)`
- `const uint8_t *map()` (whole region, nullptr on failure), `void unmap()`

## WiFi (`wifi.h`, not in `hal.h`; ESP and host only)
`namespace Wifi`, station mode for the game loader. ESP: remote builds only (`-Wl,--wrap=free`).
Host: never starts.
- `HEAP_BYTES`: driver memory the caller passes to `begin()`.
- `typedef bool (*Receive)(void *ctx, const uint8_t *data, uint32_t length)`: false stops.
- `bool begin(const char *ssid, const char *pass, uint8_t *heap, uint32_t heapBytes)`: starts
  connecting, returns at once. The heap belongs to the driver until `down()` returns.
- `bool up(uint32_t timeoutMs)`: waits for an IP, at most `timeoutMs` after `begin()`.
- `bool get(const char *url, uint8_t *buf, uint32_t bufBytes, Receive receive, void *ctx)`:
  `http://a.b.c.d[:port]/path`, received bytes to `receive`. False: request not sent.
- `void down()`: always after `begin()`; aborts if driver memory is left in the heap.

## Entry (`entry.h`, not a backend)
- the application defines `galagino_setup()` and `galagino_loop()`; the entry backend calls them.

## Attributes (`hal_attr.h`)
- `HAL_FAST_CODE`, `HAL_FAST_DATA`, `HAL_RETAINED` (survives a reset), `PROGMEM` (empty)
- `HAL_HOT_CODE`: RAM where both cores share one flash cache (RP2350), flash elsewhere (ESP32:
  no IRAM cost). For code hot on one core that the other core's flash misses would evict.
  `HAL_HOT_DATA`: its const tables, placed the same way.
- RP2350 and host: no retained RAM yet, the retained attribute is empty there, so such a variable
  is zeroed at boot like any other.

Host-only test hooks (not part of the contract): `host::setMicros(uint64_t)`, `host::advanceMicros(uint64_t)`,
`host::setPinLevel(int, bool)`, `host::ledCount()`, `host::ledFrame()`, `host::setAudioCapacity(uint32_t)`,
`host::setAudioHook(...)`, `host::audioWrites()`.
