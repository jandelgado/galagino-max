#ifndef MENU_H
#define MENU_H

#include <Arduino.h>
#include "input.h"
#include "../config.h"
#include "../machines/machineBase.h"

class Menu {
public:
  Menu() { }
  ~Menu() { }

  void init(Input *input, const machineInfo *machines, signed char machinesCount, unsigned short *framebuffer);
  void attract_resetTimer();
  bool attract_gameTimeout();
  void render_row(short row);
  void handle();
  void show_menu();

  signed char machineIndexPreselection();
  signed char machineIndexSelected();
  bool startMachine();
  bool machineIndexIsMenu();
private:
  // Max logos on screen: 3 full + 1 partial while scrolling.
  static const unsigned char LOGO_CACHE_SIZE = 4;
  static const unsigned int LOGO_PIXELS = 224 * 96;

  void menu_logo(short row, const unsigned short *img, char active);
  unsigned short convert_RGB565_to_greyscale(unsigned short in);
  void refreshLogoCache();
  const unsigned short *logoBuffer(Asset<unsigned short, COMPRESSED> &logo);
  void enterMenu();
  void leaveMenu();
#ifdef MENU_CYLINDER
  void render_row_cylinder(short row);
#endif

  Input *input;
  signed char machinesCount;
  machineBase *currentMachine;
  const machineInfo *machines;
  unsigned short *frame_buffer;
  unsigned char last_mask;
  bool menuWasSelected;
  unsigned long master_attract_timeout; // menu timeout for master attract mode which randomly start games
  signed char machineIndexLast;
  signed char machineIndex;
  signed char menu_sel;
  int scroll_offset = 0;

  // Decoded logos, allocated from Arena only while the menu shows.
  // slot_logo[i] owns logo_pool[i]. Decoding every frame is too slow, so a
  // logo decodes once and stays until scrolled off.
  unsigned char logo_pool_count = 0;
  unsigned short *logo_pool[LOGO_CACHE_SIZE] = { };
  Asset<unsigned short, COMPRESSED> *slot_logo[LOGO_CACHE_SIZE] = { };

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
};

const char *mchName(signed char machineType);
#endif
