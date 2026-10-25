#ifndef MENU_H
#define MENU_H

#include <Arduino.h>
#include "../../emulation/input.h"
#include "../../config.h"
#include "../machineBase.h"

// Game selection screen, run like any machine: main creates it, asks
// selected() each frame and deletes it to start the chosen game.
// Logos live in Arena while the menu exists.
class Menu : public machineBase {
public:
  static const int8_t NONE = -1;

  // strip starts at shown, then scrolls to the highlighted preselect
  Menu(const machineInfo *machines, int16_t machinesCount, int8_t shown, int8_t preselect);
  ~Menu() override;

  void start() override;
  void prepare_frame() override;
  void render_row(short row) override;
#ifdef LED_PIN
  void gameLeds(CRGB *leds) override;
#endif

  // chosen machine index, NONE until FIRE/START or menu attract
  int8_t selected() const { return selection; }

private:
  // Max logos on screen: 3 full + 1 partial while scrolling.
  static const unsigned char LOGO_CACHE_SIZE = 4;

  void menu_logo(short row, const uint8_t *slot, char active);
  unsigned short convert_RGB565_to_greyscale(unsigned short in);
  void refreshLogoCache();
  const uint8_t *logoBuffer(const Logo &logo);
  void enterMenu();
  void select();
  int scrollTarget(int16_t sel);
  void scroll();
#ifdef MENU_CYLINDER
  void render_row_cylinder(short row);
#endif

  const machineInfo *machines;
  int16_t machinesCount;
  int16_t menu_sel;         // highlighted machine, 1-based
  int8_t selection = NONE;
  unsigned int last_mask;
  uint32_t attract_start;   // menu attract: start menu_sel when idle
  int scroll_offset = 0;

  // Decoded logos (palette + 4 bit pixels, see Logo), allocated from Arena
  // while the menu exists.
  // slot_logo[i] owns logo_pool[i]. Decoding every frame is too slow, so a
  // logo decodes once and stays until scrolled off.
  unsigned char logo_pool_count = 0;
  uint8_t *logo_pool[LOGO_CACHE_SIZE] = { };
  const Logo *slot_logo[LOGO_CACHE_SIZE] = { };

#ifdef MENU_CYLINDER
  // per screen line of the drum, built by enterMenu(), lives in Arena
  struct CylinderLine {
    short line;            // line in the 288 line flat menu window, -1 = black
    unsigned char width;   // drawn width in pixels, centred (perspective)
    unsigned char shade;   // brightness, 255 = unshaded
    unsigned char sat;     // colour saturation, 255 = full, 0 = grey
  };
  CylinderLine *cylinder_lut = nullptr;
#endif

#ifdef ASSETS_REMOTE
  // remote games not cached yet: NET marker, menu attract may skip them
  uint32_t net_only[4] = { };  // bit per machine, count <= 128
  bool netOnly(int idx) const { return net_only[idx >> 5] & (1u << (idx & 31)); }
  void netStrip(short logo_y, int idx);
  void netLine(unsigned short *line, int logo_line);
#if NO_DOWNLOADS_IN_ATTRACT
  int16_t nextAvailable(int16_t sel);
#endif
#endif
};

const char *mchName(uint8_t machineType);
#endif
