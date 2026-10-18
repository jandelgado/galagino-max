#ifdef ASSETS_REMOTE
#include <Arduino.h>
#include <cstring>
#include <new>
#include <mbedtls/sha256.h>
#include "../config.h"
#include "../emulation/arena.h"
#include "asset_cache.h"
#include "bundle_unpack.h"
#include "http_body.h"
#include "loader.h"
#include "loader_screen.h"
#include "partition_region.h"
#include "wifi.h"

#ifndef GALAGINO_ASSETS_URL
#error "GALAGINO_ASSETS_URL (e.g. \"http://192.168.1.2:8080\") missing, set it in config_local.h"
#endif

// loader screen line, 28 chars + 0
static constexpr uint32_t LINE_BYTES = 29;

static EspPartitionRegion region("assets");
static AssetCache cache(region);

// work buffers of the download: first fit puts the dictionary into block
// B, the WiFi heap fills A. Wifi::down() resets the Arena, so no free.
static void *arenaAlloc(ISzAllocPtr, size_t size) {
  return Arena::alloc(size, 4);
}
static void arenaFree(ISzAllocPtr, void *) { }
static const ISzAlloc lzmaAlloc = {arenaAlloc, arenaFree};

// HTTP body -> SHA-256 (of the wire bundle) and unpacker -> cache
struct Download {
  mbedtls_sha256_context sha;
  BundleUnpacker *unpacker;
  LoaderScreen *screen;
  uint32_t got;
  uint32_t total;
};

static bool toUnpacker(void *ctx, const uint8_t *data, uint32_t length) {
  Download *d = (Download *)ctx;
  mbedtls_sha256_update_ret(&d->sha, data, length);
  d->got += length;
  d->screen->progress(d->got, d->total);
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

// serial log and loader screen, then false
static bool fail(LoaderScreen &screen, const char *text) {
  printf("loader: error %s\n", text);
  screen.fail(text);
  return false;
}

static bool download(const BundleRef &ref, const char *hex, LoaderScreen &screen) {
  // erase before WiFi: up to 1 MB takes seconds, no reason to hold WiFi meanwhile
  screen.line("ERASE");
  if (!cache.begin(ref)) {
    printf("loader: entry %u bytes\n", ref.entryLength);
    return fail(screen, "FLASH");
  }

  char url[128];
  snprintf(url, sizeof(url), "%s/g/%s", GALAGINO_ASSETS_URL, hex);
  printf("loader: GET %s\n", url);

  const bool connected = Wifi::up();
  screen.line(connected ? "WIFI OK" : "WIFI");
  char get[LINE_BYTES];
  snprintf(get, sizeof(get), "GET %s", hex);
  screen.line(get);
  uint8_t *dict = Arena::alloc(BundleUnpacker::PLAIN_DICT_BYTES, 4);
  void *slot = Arena::alloc(sizeof(BundleUnpacker), alignof(BundleUnpacker));
  BundleUnpacker *unpacker = new (slot) BundleUnpacker(toCache, nullptr, dict, &lzmaAlloc);

  Download d;
  d.unpacker = unpacker;
  d.screen = &screen;
  d.got = 0;
  d.total = ref.wireLength;
  mbedtls_sha256_init(&d.sha);
  mbedtls_sha256_starts_ret(&d.sha, 0);
  HttpBody body(toUnpacker, &d);

  const uint32_t t0 = millis();
  const bool got = connected && Wifi::get(url, body);
  const uint32_t ms = millis() - t0;
  // the unpacker lives in the Arena: read it before WiFi down resets that
  const bool unpacked = unpacker->finished() && unpacker->entryLength() == ref.entryLength;
  const uint32_t arenaUsed = Arena::bytesUsed();
  Wifi::down();

  uint8_t digest[32];
  mbedtls_sha256_finish_ret(&d.sha, digest);
  mbedtls_sha256_free(&d.sha);
  if (!connected) {
    return fail(screen, "WIFI NOT CONNECTED");
  }
  if (!got) {
    printf("loader: HTTP %d, %u of %u bytes\n", body.status(), body.received(), ref.wireLength);
    char msg[LINE_BYTES];
    snprintf(msg, sizeof(msg), "HTTP %d", body.status());
    return fail(screen, msg);
  }
  printf("loader: %u bytes in %u ms, entry %u bytes, Arena %u\n", body.received(), ms, ref.entryLength,
         arenaUsed);

  if (body.received() != ref.wireLength || memcmp(digest, ref.hash, sizeof(ref.hash))) {
    return fail(screen, "SHA MISMATCH");
  }
  printf("loader: SHA OK\n");
  screen.line("SHA OK");
  if (!unpacked) {
    printf("loader: bundle does not unpack to %u bytes\n", ref.entryLength);
    return fail(screen, "UNPACK");
  }
  if (!cache.commit()) {
    return fail(screen, "FLASH");
  }
  return true;
}

bool Loader::prepare(const BundleRef *ref, LoaderScreen &screen) {
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
    screen.begin(ref->name);
    if (download(*ref, hex, screen)) {
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
