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
#include "partition_region.h"
#include "wifi.h"

#ifndef GALAGINO_ASSETS_URL
#error "GALAGINO_ASSETS_URL (e.g. \"http://192.168.1.2:8080\") missing, set it in config_local.h"
#endif

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
};

static bool toUnpacker(void *ctx, const uint8_t *data, uint32_t length) {
  Download *d = (Download *)ctx;
  mbedtls_sha256_update_ret(&d->sha, data, length);
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

static bool download(const BundleRef &ref, const char *hex) {
  // erase before WiFi: up to 1 MB takes seconds, no reason to hold WiFi meanwhile
  if (!cache.begin(ref)) {
    printf("loader: error entry %u bytes does not fit the assets partition\n", ref.entryLength);
    return false;
  }

  char url[128];
  snprintf(url, sizeof(url), "%s/g/%s", GALAGINO_ASSETS_URL, hex);
  printf("loader: GET %s\n", url);

  const bool connected = Wifi::up();
  uint8_t *dict = Arena::alloc(BundleUnpacker::PLAIN_DICT_BYTES, 4);
  void *slot = Arena::alloc(sizeof(BundleUnpacker), alignof(BundleUnpacker));
  BundleUnpacker *unpacker = new (slot) BundleUnpacker(toCache, nullptr, dict, &lzmaAlloc);

  Download d;
  d.unpacker = unpacker;
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
    printf("loader: error WiFi not connected\n");
    return false;
  }
  if (!got) {
    printf("loader: error download failed, HTTP %d, %u of %u bytes\n", body.status(), body.received(),
           ref.wireLength);
    return false;
  }
  printf("loader: %u bytes in %u ms, entry %u bytes, Arena %u\n", body.received(), ms, ref.entryLength,
         arenaUsed);

  if (body.received() != ref.wireLength || memcmp(digest, ref.hash, sizeof(ref.hash))) {
    printf("loader: error SHA mismatch\n");
    return false;
  }
  printf("loader: SHA OK\n");
  if (!unpacked) {
    printf("loader: error bundle does not unpack to %u bytes\n", ref.entryLength);
    return false;
  }
  return cache.commit();
}

bool Loader::prepare(const BundleRef *ref) {
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
  else if (download(*ref, hex)) {
    entry = cache.find(*ref);
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
