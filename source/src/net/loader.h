#ifndef LOADER_H
#define LOADER_H

#include "bundle.h"

class LoaderScreen;

// Game loader, called by switchTo() before the machine ctor (old machine
// deleted, Arena empty). Makes the game's remote assets available:
//
//   ref == nullptr (built in)       -> unmount, true
//   cache holds ref                 -> mount
//   else  WiFi begins connecting while a cache slot is picked and erased,
//         WiFi up, GET /g/<hex>: SHA-256 over the bundle, PLAIN entries
//         decoded into the slot, commit the tag last, WiFi down -> mount.
//         Erase and download progress on screen.
//
// False: download failed (error shown until a button press), nothing
// mounted, caller returns to the menu.
namespace Loader {
  bool prepare(const BundleRef *ref, LoaderScreen &screen);
  // starts without network: built in or cached
  bool available(const BundleRef *ref);
}

#endif
