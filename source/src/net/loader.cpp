#ifdef ASSETS_REMOTE
#include <cstdio>
#include <cstring>
#include <new>
#include "../config.h"
#include "../emulation/arena.h"
#include "../hal/flash_region.h"
#include "../hal/hal.h"
#include "../hal/wifi.h"
#include "asset_cache.h"
#include "bundle_unpack.h"
#include "http_body.h"
#include "loader.h"
#include "sha256.h"

#ifndef GALAGINO_ASSETS_URL
#error "GALAGINO_ASSETS_URL (e.g. \"http://192.168.1.2:8080\") missing, set it in config_local.h"
#endif

// event line, 28 chars + 0 (loader screen width)
static constexpr uint32_t LINE_BYTES = 29;
// RP2350: a join takes ~4 s and may need a retry (hal/rp2350/wifi.cpp)
static constexpr uint32_t CONNECT_TIMEOUT_MS = 20000;
// from Arena, outside the WiFi heap so the driver never competes for it;
// static DRAM is too tight (solomon low-water 2.9 KB)
static constexpr uint32_t RECV_BUF_BYTES = 4096;

static hal::FlashRegion region("assets");
static AssetCache cache(region);

// work buffers of the download: first fit puts the dictionary into block
// B, the WiFi heap fills A. Reset with the Arena after WiFi down, no free.
static void *arenaAlloc(ISzAllocPtr, size_t size) {
  return Arena::alloc(size, 4);
}
static void arenaFree(ISzAllocPtr, void *) { }
static const ISzAlloc lzmaAlloc = {arenaAlloc, arenaFree};

// HTTP body -> SHA-256 (of the wire bundle) and unpacker -> cache
struct Download {
  Sha256 sha;
  BundleUnpacker *unpacker;
  LoaderEvents *events;
  uint32_t got;
  uint32_t total;
};

static bool toUnpacker(void *ctx, const uint8_t *data, uint32_t length) {
  Download *d = (Download *)ctx;
  d->sha.update(data, length);
  d->got += length;
  d->events->progress(d->got, d->total);
  return d->unpacker->feed(data, length);
}

static bool toCache(void *, const uint8_t *data, uint32_t length) {
  return cache.append(data, length);
}

static void hashHex(const BundleRef &ref, char *hex) {
  for (uint32_t i = 0; i < sizeof(ref.hash); i++) {
    snprintf(hex + 2 * i, 3, "%02x", ref.hash[i]);
  }
}

// serial log and events, then false
static bool fail(LoaderEvents &events, const char *text) {
  printf("loader: error %s\n", text);
  events.fail(text);
  return false;
}

// stop at the end of the body: a server may keep the connection open
static bool toBody(void *ctx, const uint8_t *data, uint32_t length) {
  HttpBody *body = (HttpBody *)ctx;
  return body->feed(data, length) && !body->complete();
}

// the game unpacks into the Arena next
static void wifiDown() {
  hal::Wifi::down();
  Arena::reset();
}

static void eraseProgress(void *ctx, uint32_t done, uint32_t total) {
  ((LoaderEvents *)ctx)->progress(done, total);
}

static bool download(const BundleRef &ref, const char *hex, LoaderEvents &events) {
  // connecting takes ~1.7 s: let it run while the slot erases
  uint8_t *wifiHeap = Arena::alloc(hal::Wifi::HEAP_BYTES, 4);
  const bool started = hal::Wifi::begin(GALAGINO_WIFI_SSID, GALAGINO_WIFI_PASS, wifiHeap, hal::Wifi::HEAP_BYTES);
  events.line("ERASE");
  const uint32_t eraseStart = hal::millis();
  if (!cache.begin(ref, eraseProgress, &events)) {
    wifiDown();
    printf("loader: entry %u bytes\n", ref.entryLength);
    return fail(events, "FLASH");
  }
  printf("loader: erased in %u ms\n", hal::millis() - eraseStart);

  char url[128];
  snprintf(url, sizeof(url), "%s/g/%s", GALAGINO_ASSETS_URL, hex);
  printf("loader: GET %s\n", url);

  const bool connected = started && hal::Wifi::up(CONNECT_TIMEOUT_MS);
  events.line(connected ? "WIFI OK" : "WIFI");
  char get[LINE_BYTES];
  snprintf(get, sizeof(get), "GET %s", hex);
  events.line(get);
  uint8_t *dict = Arena::alloc(BundleUnpacker::PLAIN_DICT_BYTES, 4);
  void *slot = Arena::alloc(sizeof(BundleUnpacker), alignof(BundleUnpacker));
  BundleUnpacker *unpacker = new (slot) BundleUnpacker(toCache, nullptr, dict, &lzmaAlloc);

  Download d;
  d.unpacker = unpacker;
  d.events = &events;
  d.got = 0;
  d.total = ref.wireLength;
  HttpBody body(toUnpacker, &d);

  uint8_t *recvBuf = Arena::alloc(RECV_BUF_BYTES);
  const uint32_t t0 = hal::millis();
  const bool got = connected && hal::Wifi::get(url, recvBuf, RECV_BUF_BYTES, toBody, &body) && body.complete();
  const uint32_t ms = hal::millis() - t0;
  // the unpacker lives in the Arena: read it before wifiDown() resets that
  const bool unpacked = unpacker->finished() && unpacker->entryLength() == ref.entryLength;
  const uint32_t arenaUsed = Arena::bytesUsed();
  wifiDown();

  uint8_t digest[Sha256::DIGEST_BYTES];
  d.sha.finish(digest);
  if (!connected) {
    return fail(events, "WIFI NOT CONNECTED");
  }
  if (!got) {
    printf("loader: HTTP %d, %u of %u bytes\n", body.status(), body.received(), ref.wireLength);
    char msg[LINE_BYTES];
    snprintf(msg, sizeof(msg), "HTTP %d", body.status());
    return fail(events, msg);
  }
  printf("loader: %u bytes in %u ms, entry %u bytes, Arena %u\n", body.received(), ms, ref.entryLength,
         arenaUsed);

  if (body.received() != ref.wireLength || memcmp(digest, ref.hash, sizeof(ref.hash))) {
    return fail(events, "SHA MISMATCH");
  }
  printf("loader: SHA OK\n");
  events.line("SHA OK");
  if (!unpacked) {
    printf("loader: bundle does not unpack to %u bytes\n", ref.entryLength);
    return fail(events, "UNPACK");
  }
  if (!cache.commit()) {
    return fail(events, "FLASH");
  }
  return true;
}

bool Loader::prepare(const BundleRef *ref, LoaderEvents &events) {
  Bundle::unmount();
  if (!ref) {
    return true;
  }

  char hex[17];
  hashHex(*ref, hex);
#ifdef LOADER_FORCE_DOWNLOAD
  const uint8_t *entry = nullptr;  // test: every start downloads (leak cycles)
#else
  const uint8_t *entry = cache.find(*ref);
#endif
  if (entry) {
    printf("loader: cached %s\n", hex);
  }
  else {
    events.begin(ref->name);
    if (download(*ref, hex, events)) {
      entry = cache.find(*ref);
    }
  }

  if (!entry || !Bundle::mount(entry, ref->entryLength)) {
    printf("loader: error no valid entry for %s\n", hex);
    return false;
  }
  return true;
}

bool Loader::available(const BundleRef *ref) {
  return !ref || cache.find(*ref);
}
#endif
