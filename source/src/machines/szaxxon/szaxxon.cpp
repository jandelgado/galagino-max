#include "../../catalog.h"
#ifdef GAME_SZAXXON

#include <type_traits>
#include "szaxxon.h"
#include "../zaxxon/zaxxon_dipswitches.h"
#include "szaxxon_rom_main.h"
#include "szaxxon_rom_ops.h"
#include "szaxxon_chartiles.h"
#include "szaxxon_bgtiles.h"
#include "szaxxon_spritetiles.h"
#include "szaxxon_tilemap.h"
#include "szaxxon_palette.h"

// zaxxon binds these as its own asset types: both arena plans must agree
#define SZAXXON_SAME_TYPE(a) \
  static_assert(std::is_same<decltype(szaxxon_##a##_blob), decltype(zaxxon_##a##_blob)>::value, \
                "szaxxon_" #a " planned unlike zaxxon_" #a ": rerun romconv")
SZAXXON_SAME_TYPE(bgtiles);
SZAXXON_SAME_TYPE(chartiles);
SZAXXON_SAME_TYPE(palette);
SZAXXON_SAME_TYPE(fgcolor_codes);
SZAXXON_SAME_TYPE(rom_main);
SZAXXON_SAME_TYPE(spritetiles);
SZAXXON_SAME_TYPE(tilemap_code);
SZAXXON_SAME_TYPE(tilemap_color);

// DSW02 bit 2 is Difficulty on szaxxon (MAME PORT_MODIFY: 0x04 Normal,
// 0x00 Hard); unused and clear on zaxxon
static constexpr uint8_t SZAXXON_DIP_A = ZAXXON_DIP_A | 0x04;

// resolved per machine start, not at static init: remote mode looks the
// blob up in the mounted bundle
static const uint8_t *rom_ops(void) {
  return Asset<unsigned char, PLAIN>(szaxxon_rom_ops_blob).data();
}

szaxxon::szaxxon()
  : zaxxon({szaxxon_bgtiles_blob, szaxxon_chartiles_blob, szaxxon_palette_blob,
            szaxxon_fgcolor_codes_blob, szaxxon_rom_main_blob, szaxxon_spritetiles_blob,
            szaxxon_tilemap_code_blob, szaxxon_tilemap_color_blob},
           SZAXXON_DIP_A, rom_ops()) { }

#endif // GAME_SZAXXON
