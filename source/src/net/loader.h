#ifndef LOADER_H
#define LOADER_H

#include "bundle.h"

// What the loader reports while it downloads; the UI shows it.
class LoaderEvents {
public:
  virtual ~LoaderEvents() { }
  // download of this game starts
  virtual void begin(const char *game) = 0;
  // step done, e.g. "SHA OK"
  virtual void line(const char *text) = 0;
  // erase and download progress, 0 first
  virtual void progress(uint32_t got, uint32_t total) = 0;
  // download failed; returns when the user acknowledged
  virtual void fail(const char *text) = 0;
};

// Game loader, called by switchTo() before the machine ctor (old machine
// deleted, Arena empty). Makes the game's remote assets available:
//
//   ref == nullptr (built in)       -> unmount, true
//   cache holds ref                 -> mount
//   else  WiFi begins connecting while a cache slot is picked and erased,
//         WiFi up, GET /g/<hex>: SHA-256 over the bundle, PLAIN entries
//         decoded into the slot, commit the tag last, WiFi down -> mount.
//         Erase and download progress to events.
//
// False: download failed (events.fail() returned), nothing
// mounted, caller returns to the menu.
namespace Loader {
  bool prepare(const BundleRef *ref, LoaderEvents &events);
  // starts without network: built in or cached
  bool available(const BundleRef *ref);
}

#endif
