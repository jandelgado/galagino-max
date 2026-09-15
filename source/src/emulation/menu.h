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
  const unsigned short *logoBuffer(RomData<unsigned short, COMPRESSED> &logo);
  void enterMenu();
  void leaveMenu();

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

  // Decoded logos, allocated only while the menu shows. slot_logo[i] owns
  // logo_pool[i]. One block per slot: the heap is fragmented after setup(),
  // a single combined block may not fit. Decoding every frame is too slow,
  // so a logo decodes once and stays until scrolled off. logo_pool_count
  // can be below LOGO_CACHE_SIZE: the last machine's ROM buffers stay
  // resident for audio.
  unsigned char logo_pool_count = 0;
  unsigned short *logo_pool[LOGO_CACHE_SIZE] = { };
  RomData<unsigned short, COMPRESSED> *slot_logo[LOGO_CACHE_SIZE] = { };
};

const char *mchName(signed char machineType);
#endif
