#ifndef LOADER_H
#define LOADER_H

#include "bundle.h"

// Game loader, called by switchTo() before the machine ctor (old machine
// deleted, Arena empty). Makes the game's remote assets available:
//
//   ref == nullptr (built in)       -> unmount, true
//   cache holds ref                 -> mount
//   else  pick + erase a cache slot, WiFi up, GET /g/<hex>: SHA-256 over
//         the bundle, PLAIN entries decoded into the slot, commit the tag
//         last, WiFi down -> mount
//
// False: download failed, nothing mounted, caller returns to the menu.
namespace Loader {
  bool prepare(const BundleRef *ref);
  // starts without network: built in or cached
  bool available(const BundleRef *ref);
}

#endif
