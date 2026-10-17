#ifdef ASSETS_REMOTE
#include <Arduino.h>
#include <cstring>
#include <mbedtls/sha256.h>
#include "../config.h"
#include "asset_store.h"
#include "http_body.h"
#include "loader.h"
#include "partition_region.h"
#include "wifi.h"

#ifndef GALAGINO_ASSETS_URL
#error "GALAGINO_ASSETS_URL (e.g. \"http://192.168.1.2:8080\") missing, set it in config_local.h"
#endif

static EspPartitionRegion region("assets");
static AssetStore store(region);

struct Download {
  mbedtls_sha256_context sha;
  AssetStore *store;
};

static bool toStore(void *ctx, const uint8_t *data, uint32_t length) {
  Download *d = (Download *)ctx;
  mbedtls_sha256_update_ret(&d->sha, data, length);
  return d->store->append(data, length);
}

static void hashHex(const BundleRef &ref, char *hex) {
  for (uint32_t i = 0; i < sizeof(ref.hash); i++) {
    snprintf(hex + 2 * i, 3, "%02x", ref.hash[i]);
  }
}

static bool download(const BundleRef &ref, const char *hex) {
  // erase before WiFi: 720 KB takes ~2 s, no reason to hold WiFi meanwhile
  if (!store.begin(ref)) {
    printf("loader: error bundle %u bytes does not fit the assets partition\n", ref.length);
    return false;
  }

  char url[128];
  snprintf(url, sizeof(url), "%s/g/%s", GALAGINO_ASSETS_URL, hex);
  printf("loader: GET %s\n", url);

  Download d;
  d.store = &store;
  mbedtls_sha256_init(&d.sha);
  mbedtls_sha256_starts_ret(&d.sha, 0);
  HttpBody body(toStore, &d);

  const bool connected = Wifi::up();
  const uint32_t t0 = millis();
  const bool got = connected && Wifi::get(url, body);
  const uint32_t ms = millis() - t0;
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
           ref.length);
    return false;
  }
  printf("loader: %u bytes in %u ms\n", body.received(), ms);

  if (memcmp(digest, ref.hash, sizeof(ref.hash))) {
    printf("loader: error SHA mismatch\n");
    return false;
  }
  printf("loader: SHA OK\n");
  return store.commit();
}

bool Loader::prepare(const BundleRef *ref) {
  Bundle::unmount();
  if (!ref) {
    return true;
  }

  char hex[17];
  hashHex(*ref, hex);
#ifdef LOADER_FORCE_DOWNLOAD
  const bool cached = false;  // test: every start downloads (leak cycles)
#else
  const bool cached = store.holds(*ref);
#endif
  if (cached) {
    printf("loader: cached %s\n", hex);
  }
  else if (!download(*ref, hex)) {
    return false;
  }

  const uint8_t *bundle = store.bundle();
  if (!bundle || !Bundle::mount(bundle, ref->length)) {
    printf("loader: error bundle header invalid\n");
    return false;
  }
  return true;
}
#endif
