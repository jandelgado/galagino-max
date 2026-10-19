#ifndef LOADER_H
#define LOADER_H

#include "bundle.h"

// Game loader, called by switchTo() before the machine ctor (old machine
// deleted, Arena empty). Makes the game's PLAIN assets available:
//
//   ref == nullptr (built in)       -> unmount, true
//   assets partition holds ref      -> mount
//   else  erase, WiFi up, GET /g/<hex> into the partition with SHA-256,
//         commit tag last, WiFi down -> mount
//
// False: download failed, nothing mounted, caller returns to the menu.
namespace Loader {
  bool prepare(const BundleRef *ref);
}

#endif
