#include "menu.h"
#include "../../emulation/arena.h"
#ifdef ASSETS_REMOTE
#include "../../emulation/hud.h"
#include "../../net/loader.h"

// NET marker, bottom right of a logo (224 x 96): green text, dark gray box
static constexpr short NET_W = 3 * HUD_GLYPH_SIZE + 4;
static constexpr short NET_H = HUD_GLYPH_SIZE + 4;
static constexpr short NET_X = 224 - NET_W - 2;
static constexpr short NET_Y = 96 - NET_H - 2;
#endif
#ifdef MENU_CYLINDER
#include <math.h>

// Drum geometry: the middle logo (+-CYL_FLAT lines around the screen
// centre) is a flat front face, drawn 1:1. Beyond it the surface bends
// away on an arc of radius CYL_RADIUS up to CYL_ANGLE, so each screen line
// there skips more logo lines. The arc covers exactly one logo per side,
// so the visible logo lines stay within the flat 288 line window and the
// 4 slot logo pool still suffices. Rest of the screen is black.
// Fake perspective on the arc: lines narrow towards the rim (by up to
// CYL_NARROW of the width), darken with the surface angle (down to
// CYL_AMBIENT brightness). Saturation falls linearly with screen distance
// from the flat part, grey at the rim.
constexpr float CYL_FLAT = 48.0f;
constexpr float CYL_ANGLE = 80.0f * (float)M_PI / 180.0f;
constexpr float CYL_RADIUS = (144.0f - CYL_FLAT) / CYL_ANGLE;
constexpr float CYL_NARROW = 0.45f;
constexpr float CYL_AMBIENT = 0.25f;

// byte swapped RGB565 as stored in the logos (see
// convert_RGB565_to_greyscale()), r and b 5 bit, g 6 bit
static inline unsigned short pack_RGB565(unsigned short r, unsigned short g, unsigned short b) {
  return ((g << 13) & 0xe000) | ((b << 8) & 0x1f00) | ((r << 3) & 0x00f8) | ((g >> 3) & 0x0007);
}

// blend towards luma by sat, then scale by shade; 255 leaves either unchanged
static inline unsigned short shade_RGB565(unsigned short in, uint8_t shade, uint8_t sat) {
  const int32_t m = shade + 1;
  const int32_t k = sat + 1;
  // 6 bit per channel
  int32_t r = ((in >> 3) & 31) << 1;
  int32_t g = ((in << 3) & 0x38) | ((in >> 13) & 0x07);
  int32_t b = ((in >> 8) & 31) << 1;
  int32_t y = (77 * r + 150 * g + 29 * b) >> 8;
  r = (((y << 8) + (r - y) * k) * m) >> 16;
  g = (((y << 8) + (g - y) * k) * m) >> 16;
  b = (((y << 8) + (b - y) * k) * m) >> 16;
  return pack_RGB565(r >> 1, g, b >> 1);
}

#endif

Menu::Menu(const machineInfo *machines, int8_t machinesCount, int8_t shown, int8_t preselect)
  : machines(machines), machinesCount(machinesCount), menu_sel(preselect + 1) {
  attract_start = millis();

  // buttons still held from the reset combo (e.g. nunchuck C+Z) must be
  // released before they count
  last_mask = ~0u;

  // start at the last game, scroll() moves on to the preselection
  if(machinesCount > 3) {
    scroll_offset = scrollTarget(shown + 1);
  }

  enterMenu();

#ifdef ASSETS_REMOTE
  // once: a cache lookup scans flash
  for(int i = 0; i < machinesCount; i++) {
    if(!Loader::available(machines[i].bundle)) {
      net_only[i >> 5] |= 1u << (i & 31);
    }
  }
#if NO_DOWNLOADS_IN_ATTRACT
  // highlight the game the menu attract will start
  menu_sel = nextAvailable(menu_sel);
#endif
#endif
}

// Logo pool dies with the menu: the next machine unpacks into a clean Arena.
Menu::~Menu() {
  Arena::reset();
}

// no CPU to boot: sync to vblank right away
void Menu::start() {
  game_started = 1;
}

void Menu::select() {
  selection = menu_sel - 1;
  printf("select machine #%d - %s\n", selection, mchName(machines[selection].type));
}

void Menu::prepare_frame() {
  if(selection != NONE) { return; }

  const unsigned int keymask = input->buttons_get();
  const unsigned int pressed = keymask & ~last_mask;
  last_mask = keymask;

  // navigation restarts the menu attract countdown
  if(pressed & (BUTTON_UP | BUTTON_DOWN | BUTTON_EXTRA)) {
    attract_start = millis();
  }

  if(pressed & BUTTON_UP) { menu_sel--; }
  if(pressed & BUTTON_DOWN) { menu_sel++; }

  // up to 3 logos fit the screen: clamp; more scroll: wrap
  if(machinesCount <= 3) {
    if(menu_sel < 1) { menu_sel = 1; }
    if(menu_sel > machinesCount) { menu_sel = machinesCount; }
  }
  else {
    if(menu_sel < 1) { menu_sel = machinesCount; }
    if(menu_sel > machinesCount) { menu_sel = 1; }
  }

  scroll();

  if(pressed & (BUTTON_FIRE | BUTTON_START)) {
    select();
    return;
  }

#ifdef MASTER_ATTRACT_MENU_TIMEOUT
  if(millis() - attract_start > MASTER_ATTRACT_MENU_TIMEOUT) {
#if defined(ASSETS_REMOTE) && NO_DOWNLOADS_IN_ATTRACT
    // no downloads while idle: network traffic, cache churn, flash wear.
    // Highlight moves on to the next downloaded game, which starts once
    // scrolled into the middle.
    menu_sel = nextAvailable(menu_sel);
    if(machinesCount > 3 && scroll_offset != scrollTarget(menu_sel)) { return; }
#endif
    printf("MASTER ATTRACT ");
    select();
  }
#endif
}

// Moves the strip towards the highlighted logo, the short way round. The
// highlight stays at most one logo from the middle slot, so on screen
// however fast UP/DOWN repeat: a longer way jumps the rest.
void Menu::scroll() {
  static const int STEP = 12;  // lines per screen, divides 96

  if(machinesCount <= 3) { return; }

  const int total = machinesCount * 96;
  int diff = (scrollTarget(menu_sel) - scroll_offset) % total;
  if(diff < 0) { diff += total; }
  if(diff > total / 2) { diff -= total; }

  int jump = 0;
  if(diff > 96) { jump = diff - 96; }
  if(diff < -96) { jump = diff + 96; }
  int step = diff - jump;
  if(step > STEP) { step = STEP; }
  if(step < -STEP) { step = -STEP; }
  scroll_offset = (scroll_offset + jump + step + total) % total;
}

// scroll_offset that puts machine sel (1-based) in the middle slot
int Menu::scrollTarget(int8_t sel) {
  if(sel == 1) {
    return (machinesCount - 1) * 96;
  }
  return 96 * ((unsigned)(sel - 2) % machinesCount);
}

// Pool lives as long as the menu. The last machine is deleted by now, so
// reset() reclaims its assets.
void Menu::enterMenu() {
  static_assert(LOGO_CACHE_SIZE * Logo::SLOT_BYTES <= Arena::BLOCK_A_SIZE, "logo pool fits block A");
  Arena::reset();
  for(logo_pool_count = 0; logo_pool_count < LOGO_CACHE_SIZE; logo_pool_count++) {
    logo_pool[logo_pool_count] = Arena::alloc(Logo::SLOT_BYTES, alignof(uint16_t));
    slot_logo[logo_pool_count] = nullptr;
  }

#ifdef MENU_CYLINDER
  // after the pool, same block
  static_assert(LOGO_CACHE_SIZE * Logo::SLOT_BYTES + 288 * sizeof(CylinderLine) <= Arena::BLOCK_A_SIZE,
                "logo pool and drum table fit block A");
  cylinder_lut = Arena::alloc<CylinderLine>(288);
  for(short y = 0; y < 288; y++) {
    CylinderLine &l = cylinder_lut[y];
    float d = y + 0.5f - 144.0f;   // line centre, relative to screen centre
    float a = fabsf(d);
    float s = a;
    float t = 0.0f;                // surface angle towards the viewer
    float f = 0.0f;                // screen distance from the flat part, 1 = rim
    if(a > CYL_FLAT) {
      float u = (a - CYL_FLAT) / CYL_RADIUS;
      f = u / sinf(CYL_ANGLE);
      if(u > sinf(CYL_ANGLE)) {
        l.line = -1;
        continue;
      }
      t = asinf(u);
      s = CYL_FLAT + CYL_RADIUS * t;
    }
    short line = (short)floorf(144.0f + (d < 0 ? -s : s));
    float c = cosf(t);
    l.line = line < 0 ? 0 : (line > 287 ? 287 : line);
    l.width = (unsigned char)lroundf(224.0f * (1.0f - CYL_NARROW * (1.0f - c)));
    l.shade = (unsigned char)lroundf(255.0f * (CYL_AMBIENT + (1.0f - CYL_AMBIENT) * c));
    l.sat = (unsigned char)lroundf(255.0f * (1.0f - f));
  }
#endif
}

void Menu::render_row(short row) {
  if(row == 0) {
    refreshLogoCache();
  }

  if(machinesCount <= 3) {
    // non-scrolling menu for 2 or 3 machines
    for(char i = 0; i < machinesCount; i++) {
      char offset = i * 12;
      if(machinesCount == 2) offset += 6;
      if(row >= offset && row < offset + 12) {
	menu_logo(8 * (row - offset), logoBuffer(machines[i].logo()), menu_sel == i + 1);
#ifdef ASSETS_REMOTE
	netStrip(8 * (row - offset), i);
#endif
      }
    }
  }
  else {
    // scrolling menu for more than 3 machines
    // valid scroll_offset values range from 0 to MACHINE * 96 - 1

#ifdef MENU_CYLINDER
    render_row_cylinder(row);
#else
    // check which logo would show up in this row. Actually
    // two may show up in the same character row when scrolling
    int logo_idx = ((row + scroll_offset / 8) / 12) % machinesCount;
    if(logo_idx < 0) logo_idx += machinesCount;

    int logo_y = (row * 8 + scroll_offset) % 96;  // logo line in this row

    // check if logo at logo_y shows up in current row
    menu_logo(logo_y, logoBuffer(machines[logo_idx].logo()), (menu_sel-1) == logo_idx);
#ifdef ASSETS_REMOTE
    netStrip(logo_y, logo_idx);
#endif

    // check if a second logo may show up here
    if(logo_y > (96 - 8)) {
      logo_idx = (logo_idx + 1) % machinesCount;
      logo_y -= 96;
      menu_logo(logo_y, logoBuffer(machines[logo_idx].logo()), (menu_sel-1) == logo_idx);
#ifdef ASSETS_REMOTE
      netStrip(logo_y, logo_idx);
#endif
    }
#endif
  }

#if defined(MASTER_ATTRACT_MENU_TIMEOUT) && defined(MASTER_ATTRACT_MENU_SHOW_COUNTDOWN)
  if(row == 0) {
    // elapsed can exceed the timeout for one frame at the deadline.
    // Signed math clamps to 0; unsigned would wrap.
    long elapsed = (long)(millis() - attract_start);
    long t_remaining = elapsed >= (long)MASTER_ATTRACT_MENU_TIMEOUT ? 0 : MASTER_ATTRACT_MENU_TIMEOUT - elapsed;
    int bar = (int)(224L * t_remaining / MASTER_ATTRACT_MENU_TIMEOUT);
    for(int x=0; x<bar && x<224; x++) {
        frame_buffer[x] = MASTER_ATTRACT_MENU_COUNTDOWN_BAR_COLOR565;
    }
  }
#endif
}

// render one of three the menu logos. Only the active one is colorful
// render logo into current buffer starting with line "row" of the logo
void Menu::menu_logo(short row, const uint8_t *slot, char active) {
  // less than 8 rows in image left?
  unsigned short pix2draw = ((row <= 96 - 8) ? (224 * 8) : ((96 - row) * 224));

  unsigned short ipix = (row < 0) ? (unsigned short)(-row * 224) : 0;

  // Logo got no pool slot.
  if(!slot) {
    while(ipix < pix2draw) { frame_buffer[ipix++] = 0; }
    return;
  }

  // grey once per palette entry, not per pixel
  const uint16_t *palette = Logo::palette(slot);
  uint16_t colors[Logo::COLORS];
  for(uint8_t c = 0; c < Logo::COLORS; c++) {
    colors[c] = active ? palette[c] : convert_RGB565_to_greyscale(palette[c]);
  }

  // logo pixel shown at frame_buffer[0], negative above the logo
  const int32_t first = row * 224;
  const uint8_t *pixels = Logo::pixels(slot);
  for(; ipix < pix2draw; ipix++) {
    frame_buffer[ipix] = colors[Logo::colorIndex(pixels, first + ipix)];
  }
}

#ifdef MENU_CYLINDER
// draw 8 screen lines of the drum: each line picks its logo line from the
// flat menu window via cylinder_lut, scaled to its width around the centre
void Menu::render_row_cylinder(short row) {
  const int total = machinesCount * 96;
  unsigned short *dst = frame_buffer;

  for(short y = row * 8; y < row * 8 + 8; y++, dst += 224) {
    const CylinderLine &l = cylinder_lut[y];
    const uint8_t *slot = nullptr;
    uint32_t first = 0;  // first logo pixel of this line
    int v = 0;
    if(l.line >= 0) {
      v = (scroll_offset + l.line) % total;
      slot = logoBuffer(machines[v / 96].logo());
      first = 224 * (v % 96);
    }

    if(!slot) {
      for(short x = 0; x < 224; x++) { dst[x] = 0; }
      continue;
    }

    const short left = (224 - l.width) / 2;
    const short right = left + l.width;
    for(short x = 0; x < left; x++) { dst[x] = 0; }
    for(short x = right; x < 224; x++) { dst[x] = 0; }

    // 16.16 source step, sampled at pixel centres
    const uint32_t step = (224u << 16) / l.width;
    uint32_t sx = step / 2;
    const uint16_t *palette = Logo::palette(slot);
    const uint8_t *pixels = Logo::pixels(slot);
    if(l.shade == 255 && l.sat == 255) {
      for(short x = left; x < right; x++, sx += step) {
        dst[x] = palette[Logo::colorIndex(pixels, first + (sx >> 16))];
      }
    }
    else {
      // Shading every pixel (~80 instructions) made the drum slower than
      // SPI (51 Hz): shade each palette entry once per line, on first use.
      uint16_t shaded[Logo::COLORS];
      uint16_t done = 0;  // bit c set: shaded[c] valid
      for(short x = left; x < right; x++, sx += step) {
        const uint8_t c = Logo::colorIndex(pixels, first + (sx >> 16));
        if(!(done & (1u << c))) {
          shaded[c] = shade_RGB565(palette[c], l.shade, l.sat);
          done |= 1u << c;
        }
        dst[x] = shaded[c];
      }
    }
#ifdef ASSETS_REMOTE
    // flat front face only: on the arc the marker would need the perspective
    if(l.width == 224 && netOnly(v / 96)) {
      netLine(dst, v % 96);
    }
#endif
  }
}

#endif

#ifdef ASSETS_REMOTE
// marker over the 8 lines of the current strip, logo line logo_y at the top
void Menu::netStrip(short logo_y, int idx) {
  if(!netOnly(idx)) {
    return;
  }
  for(short l = 0; l < 8; l++) {
    netLine(frame_buffer + l * 224, logo_y + l);
  }
}

// one screen line of the marker, logo_line: line of the logo it shows
void Menu::netLine(unsigned short *line, int logo_line) {
  if(logo_line < NET_Y || logo_line >= NET_Y + NET_H) {
    return;
  }
  for(short x = NET_X; x < NET_X + NET_W; x++) {
    line[x] = HUD_DARKGRAY;
  }

  const int glyph_line = logo_line - NET_Y - 2;
  if(glyph_line < 0 || glyph_line >= HUD_GLYPH_SIZE) {
    return;
  }
  const char *text = "NET";
  for(short c = 0; text[c]; c++) {
    uint8_t bits = hud_glyph_line(text[c], glyph_line);
    for(short x = NET_X + 2 + c * HUD_GLYPH_SIZE; bits; bits <<= 1, x++) {
      if(bits & 0x80) {
        line[x] = HUD_GREEN;
      }
    }
  }
}

#if NO_DOWNLOADS_IN_ATTRACT
// sel (1-based) or the next game that starts without network
int8_t Menu::nextAvailable(int8_t sel) {
  for(int8_t i = 0; i < machinesCount; i++) {
    const int8_t idx = (sel - 1 + i) % machinesCount;
    if(!netOnly(idx)) {
      return idx + 1;
    }
  }
  return sel;
}
#endif
#endif

// Decode only logos visible this frame. Caching all ~50 does not fit in RAM.
void Menu::refreshLogoCache() {
  const Logo *needed[LOGO_CACHE_SIZE] = { };
  unsigned char needed_count = 0;

  auto addNeeded = [&](signed char idx) {
    const Logo *r = &machines[idx].logo();
    for(unsigned char i = 0; i < needed_count; i++) {
      if(needed[i] == r) { return; }
    }
    if(needed_count < LOGO_CACHE_SIZE) {
      needed[needed_count++] = r;
    }
  };

  if(machinesCount <= 3) {
    for(signed char i = 0; i < machinesCount; i++) {
      addNeeded(i);
    }
  } else {
    for(short row = 0; row < 36; row++) {
      int logo_idx = ((row + scroll_offset / 8) / 12) % machinesCount;
      if(logo_idx < 0) { logo_idx += machinesCount; }
      addNeeded(logo_idx);

      if((row * 8 + scroll_offset) % 96 > (96 - 8)) {
        addNeeded((logo_idx + 1) % machinesCount);
      }
    }
  }

  // Only cache misses decode.
  for(unsigned char n = 0; n < needed_count; n++) {
    bool resident = false;
    for(unsigned char i = 0; i < logo_pool_count; i++) {
      if(slot_logo[i] == needed[n]) {
        resident = true;
        break;
      }
    }
    if(resident) { continue; }

    // Evict an unneeded slot, else the logo closest to scrolling off
    // (lowest needed[] index).
    unsigned char victim = 0;
    signed char victim_rank = needed_count;
    for(unsigned char i = 0; i < logo_pool_count; i++) {
      signed char rank = -1;
      for(unsigned char j = 0; j < needed_count; j++) {
        if(slot_logo[i] == needed[j]) {
          rank = j;
          break;
        }
      }
      if(rank < victim_rank) {
        victim = i;
        victim_rank = rank;
        if(rank == -1) { break; }
      }
    }

    needed[n]->decodeInto(logo_pool[victim]);
    slot_logo[victim] = needed[n];
  }
}

// nullptr unless refreshLogoCache() made the logo resident this frame.
const uint8_t *Menu::logoBuffer(const Logo &logo) {
  for(unsigned char i = 0; i < LOGO_CACHE_SIZE; i++) {
    if(slot_logo[i] == &logo) {
      return logo_pool[i];
    }
  }
  return nullptr;
}

unsigned short Menu::convert_RGB565_to_greyscale(unsigned short in) {
  unsigned short r = (in >> 3) & 31;
  unsigned short g = ((in << 3) & 0x38) | ((in >> 13) & 0x07);
  unsigned short b = (in >> 8) & 31;
  unsigned short avg = (2 * r + g + 2 * b) / 4;

  return (((avg << 13) & 0xe000) |   // g2-g0
          ((avg <<  7) & 0x1f00) |   // b5-b0
          ((avg <<  2) & 0x00f8) |   // r5-r0
          ((avg >>  3) & 0x0007));   // g5-g3
}

#ifdef LED_PIN
// preview of the highlighted game
void Menu::gameLeds(CRGB *leds) {
  machines[menu_sel - 1].menuLeds(leds);
}
#endif

const char *mchName(signed char machineType) {

  switch (machineType) {
    case MCH_MENU:          return "Menu";
    case MCH_PACMAN:        return "Pacman";
    case MCH_GALAGA:        return "Galaga";
    case MCH_DKONG:         return "Donkey Kong";
    case MCH_FROGGER:       return "Frogger";
    case MCH_DIGDUG:        return "Dig Dug";
    case MCH_1942:          return "1942";
    case MCH_EYES:          return "Eyes";
    case MCH_MRTNT:         return "Mr. TNT";
    case MCH_LIZWIZ:        return "Lizard Wizard";
    case MCH_THEGLOB:       return "The Glob";
    case MCH_CRUSH:         return "Crush Roller";
    case MCH_ANTEATER:      return "Ant Eater";
    case MCH_BOMBJACK:      return "Bomb Jack";
    case MCH_MRDO:          return "Mr. Do!";
    case MCH_BAGMAN:        return "Bagman";
    case MCH_PENGO:         return "Pengo";
    case MCH_MSPACMAN:      return "Ms. Pac-Man";
    case MCH_GALAXIAN:      return "Galaxian";
    case MCH_LADYBUG:       return "Lady Bug";
    case MCH_SPACEINVADERS: return "Space Invaders";
    case MCH_TIMEPLT:       return "Time Pilot";
    case MCH_GYRUSS:        return "Gyruss";
    case MCH_TUTANKHM:      return "Tutankam";
    case MCH_DKONGJR:       return "Donkey Kong Jr.";
    case MCH_STARFORCE:     return "Star Force";
    case MCH_MOONCRESTA:    return "Moon Cresta";
    case MCH_SCRAMBLE:      return "Scramble";
    case MCH_SUPERCOBRA:    return "Super Cobra";
    case MCH_DKONG3:        return "Donkey Kong 3";
    case MCH_POOYAN:        return "Pooyan";
    case MCH_PHOENIX:       return "Phoenix";
    case MCH_BURGERTIME:    return "Burger Time";
    case MCH_XEVIOUS:       return "Xevious";
    case MCH_BNJ:           return "Bump 'n' Jump";
    case MCH_MAPPY:         return "Mappy";
    case MCH_GAPLUS:        return "Gaplus";
    case MCH_ALIBABA:       return "Ali Baba and 40 Thieves";
    case MCH_AMIDAR:        return "Amidar";
    case MCH_TURTLES:       return "Turtles";
    case MCH_TP84:          return "Time Pilot '84";
    case MCH_DEFENDER:      return "Defender";
    case MCH_ROBOTRON:      return "Robotron: 2084";
    case MCH_JOUST:         return "Joust";
    case MCH_SINISTAR:      return "Sinistar";
    case MCH_YIEAR:         return "Yie Ar Kung-Fu";
    case MCH_TRACKFLD:      return "Track & Field";
    case MCH_TRON:          return "Tron";
    case MCH_DOTRON:        return "Discs of Tron";
    case MCH_ELEVATOR:      return "Elevator Action";
    case MCH_MHAVOC:        return "Major Havoc";
    case MCH_SHOLLOW:       return "Satan's Hollow";
    case MCH_MOTOS:         return "Motos";
    case MCH_JUNGLEH:       return "Jungle Hunt";
    case MCH_MEGAZONE:      return "Mega Zone";
    case MCH_JUNOFRST:      return "Juno First";
    case MCH_CHOPLIFT:      return "Choplifter";
    case MCH_TOKISENS:      return "Toki no Senshi";
    case MCH_TAPPER:        return "Tapper";
    case MCH_DOCASTLE:      return "Mr. Do's Castle";
    case MCH_MARIO:         return "Mario Bros.";
    case MCH_SELFTEST:      return "Self Test";
    case MCH_CIRCUSC:       return "Circus Charlie";
    case MCH_ROCNROPE:      return "Roc'n Rope";
    case MCH_TODRUAGA:      return "Tower of Druaga";
    case MCH_DIGDUG2:       return "Dig Dug II";
    case MCH_VANVAN:        return "Van Van Car";
    case MCH_PBACTION:      return "Pinball Action";
    case MCH_MOTORACE:      return "Moto Race USA";
    case MCH_ROADFIGHTER:   return "Road Fighter";
    case MCH_FANTASY:       return "Fantasy";
    case MCH_NIBBLER:       return "Nibbler";
    case MCH_SCREGG:        return "Scrambled Egg";
    case MCH_VANGUARD:      return "Vanguard";
    case MCH_ZAXXON:        return "Zaxxon";
    case MCH_CENTIPEDE:     return "Centipede";
    case MCH_MILLIPEDE:     return "Millipede";
    case MCH_ASTEROIDS:     return "Asteroids";
    case MCH_BALUBA:        return "Baluba-louk";
    case MCH_COMMANDO:      return "Commando";
    case MCH_GNG:           return "Ghosts'n Goblins";
    case MCH_LLANDER:       return "Lunar Lander";
    case MCH_THEPIT:        return "The Pit";
    case MCH_BUBLBOBL:      return "Bubble Bobble";
    case MCH_ATETRIS:       return "Tetris";
    case MCH_CCASTLES:      return "Crystal Castles";
    case MCH_BZONE:         return "Battle Zone";
    case MCH_TEMPEST:       return "Tempest";
    case MCH_SPACDUEL:      return "Space Duel";
    case MCH_GRAVITAR:      return "Gravitar";
    case MCH_BWIDOW:        return "Black Widow";
    case MCH_BERZERK:       return "Berzerk";
    case MCH_GORF:          return "Gorf";
    case MCH_SOLOMON:       return "Solomon's Key";
    case MCH_FLICKY:        return "Flicky";
    case MCH_PITFALL2:      return "Pitfall II";
    case MCH_MRVIKING:     return "Mister Viking";
    case MCH_IMSORRY:      return "I'm Sorry";
    case MCH_WBOY:         return "Wonder Boy";
    case MCH_FOURDWARRIO:  return "4-D Warriors";
    case MCH_HVYMETAL:     return "Heavy Metal";
    case MCH_SXEVIOUS:     return "Super Xevious";
    case MCH_SZAXXON:      return "Super Zaxxon";
    case MCH_CONTRA:       return "Contra";
    case MCH_QBERT:        return "Q*bert";
    case MCH_PLEIADS:      return "Pleiads";
    case MCH_GARDIA:       return "Gardia";
    case MCH_CAVENGER:     return "Cosmic Avenger";
    case MCH_VULGUS:       return "Vulgus";
    case MCH_MIKIE:        return "Mikie";
    case MCH_ARKANOID:     return "Arkanoid";
    case MCH_STARWARS:      return "Star Wars";
    case MCH_MISSILE:       return "Missile Command";
    case MCH_MPATROL:       return "Moon Patrol";
    case MCH_STARJACK:      return "Star Jacker";
    case MCH_REGULUS:       return "Regulus";
    case MCH_UPNDOWN:       return "Up'n Down";
    case MCH_RAFLESIA:      return "Rafflesia";
    case MCH_SWAT:          return "SWAT";
    case MCH_1943:          return "1943";
    case MCH_CBDASH:        return "Boulder Dash";
    case MCH_PHOZON:        return "Phozon";
    case MCH_GBERET:        return "Green Beret";
    case MCH_MRGOEMON:      return "Mr. Goemon";
    case MCH_JAILBREK:      return "Jail Break";
    case MCH_SCOTRSHT:      return "Scooter Shooter";
    case MCH_HERO:          return "H.E.R.O.";
    case MCH_MARBLE:        return "Marble Madness";
    case MCH_KRULL:         return "Krull";
  }

  return "";
}
