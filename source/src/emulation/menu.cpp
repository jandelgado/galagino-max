#include "menu.h"
#include "arena.h"

void Menu::init(Input *input, const machineInfo *machines,  signed char machinesCount, unsigned short *framebuffer) {
  this->master_attract_timeout = millis();
  this->input = input;
  this->machines = machines;
  this->machinesCount = machinesCount;
  this->frame_buffer = framebuffer;
  if (machinesCount == 1) {
    machineIndex = 1;
    menu_sel = 1;
  }
  else {
    machineIndex = MCH_MENU;
    menu_sel = 2;
    enterMenu();
  }
}

void Menu::show_menu() {
  machineIndex = MCH_MENU;
  menuWasSelected = true;

  // when going back to menu, reactivate attract mode
  master_attract_timeout = millis();

  // prevent start after a reset: buttons still held from the reset
  // combo (e.g. nunchuck C+Z) must be released before they count
  last_mask = 0xff;
  printf("show menu\n");

  enterMenu();
}

signed char Menu::machineIndexSelected() {
  return machineIndex - 1;
}

signed char Menu::machineIndexPreselection() {
  return menu_sel - 1;
}

bool Menu::startMachine() {
  if(machineIndex != machineIndexLast | menuWasSelected) {
    machineIndexLast = machineIndex;
    menuWasSelected = false;
    leaveMenu();
    return true;
  }
  return false;
}

// Pool lives for the whole menu session. The last machine is deleted by
// now, so reset() reclaims its assets.
void Menu::enterMenu() {
  if(logo_pool[0]) { return; }

  Arena::reset();
  for(logo_pool_count = 0; logo_pool_count < LOGO_CACHE_SIZE; logo_pool_count++) {
    // +1: slack in case uzlib writes past dest_limit.
    logo_pool[logo_pool_count] = Arena::alloc<unsigned short>(LOGO_PIXELS + 1);
    slot_logo[logo_pool_count] = nullptr;
  }
}

// Free before the machine's assets claim the arena.
void Menu::leaveMenu() {
  for(unsigned char i = 0; i < LOGO_CACHE_SIZE; i++) {
    logo_pool[i] = nullptr;
    slot_logo[i] = nullptr;
  }
  logo_pool_count = 0;
  Arena::reset();
}

bool Menu::machineIndexIsMenu() {
  return machineIndex == MCH_MENU;
}

bool Menu::attract_gameTimeout() {
#ifdef MASTER_ATTRACT_GAME_TIMEOUT
  if(machinesCount > 1 && master_attract_timeout && (millis() - master_attract_timeout > MASTER_ATTRACT_GAME_TIMEOUT)) {
    master_attract_timeout = millis();

    // select next attract machine
    menu_sel++;
    if (menu_sel > machinesCount)
      menu_sel = 1;

    printf("MASTER ATTRACT game timeout, return to menu\n");
    printf("Heap: Free=%d MaxAlloc=%d MinFree=%d\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getMinFreeHeap());
    return true;
  }
#endif
  return false;
}

void Menu::attract_resetTimer() {
  if (machineIndexIsMenu()) {
    // in menu: restart the countdown instead of disabling it
    master_attract_timeout = millis();
  }
  else if (master_attract_timeout != 0) {
    master_attract_timeout = 0;
    printf("MASTER ATTRACT timer reset!!!\n");
  }
}

void Menu::handle() {
  // get a mask of currently pressed keys
  unsigned char keymask = input->buttons_get();

  if((keymask & BUTTON_UP) && !(last_mask & BUTTON_UP))
    menu_sel--;

  if((keymask & BUTTON_DOWN) && !(last_mask & BUTTON_DOWN))
    menu_sel++;

  if(((keymask & BUTTON_FIRE) && !(last_mask & BUTTON_FIRE)) || ((keymask & BUTTON_START) && !(last_mask & BUTTON_START))) {
    machineIndex = menu_sel;
    printf("select machine #%d -> %d - %s\n",
      machineIndex,
      machines[machineIndexSelected()].type,
      mchName(machines[machineIndexSelected()].type));
  }

  if(machinesCount <= 3) {
    if(menu_sel < 1)
      menu_sel = 1;
    if(menu_sel > machinesCount)
      menu_sel = machinesCount;
  }
  else {
    if(menu_sel < 1)
      menu_sel = machinesCount;
    if(menu_sel > machinesCount)
      menu_sel = 1;
  }
  last_mask = keymask;

#ifdef MASTER_ATTRACT_MENU_TIMEOUT
  // check for master attract timeout
  if(master_attract_timeout && (millis() - master_attract_timeout > MASTER_ATTRACT_MENU_TIMEOUT)) {
    master_attract_timeout = millis();  // new timeout for running game

    machineIndex = menu_sel;
    printf("MASTER ATTRACT to machine #%d -> type %d - %s\n",
      machineIndex,
      machines[machineIndexSelected()].type,
      mchName(machines[machineIndexSelected()].type));
    printf("Heap: Free=%d MaxAlloc=%d MinFree=%d\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getMinFreeHeap());
  }
#endif
}

void Menu::render_row(short row) {
  if(row == 0) {
    refreshLogoCache();
  }

  if(machinesCount <= 3) {
    // non-scrolling menu for 2 or 3 machines. 2 machines leave rows
    // 0-5 and 30-35 empty: clear them, else the previous strip
    // (e.g. the countdown bar) repeats there
    if(machinesCount == 2 && (row < 6 || row >= 30))
      memset(frame_buffer, 0, 224 * 8 * sizeof(unsigned short));
    for(char i = 0; i < machinesCount; i++) {
      char offset = i * 12;
      if(machinesCount == 2) offset += 6;
      if(row >= offset && row < offset + 12)
	menu_logo(8 * (row - offset), logoBuffer(machines[i].logo()), menu_sel == i + 1);
    }
  }
  else {
    // scrolling menu for more than 3 machines
    // valid scroll_offset values range from 0 to MACHINE * 96 - 1

    // check which logo would show up in this row. Actually
    // two may show up in the same character row when scrolling
    int logo_idx = ((row + scroll_offset / 8) / 12) % machinesCount;
    if(logo_idx < 0) logo_idx += machinesCount;

    int logo_y = (row * 8 + scroll_offset) % 96;  // logo line in this row

    // check if logo at logo_y shows up in current row
    menu_logo(logo_y, logoBuffer(machines[logo_idx].logo()), (menu_sel-1) == logo_idx);

    // check if a second logo may show up here
    if(logo_y > (96 - 8)) {
      logo_idx = (logo_idx + 1) % machinesCount;
      logo_y -= 96;
      menu_logo(logo_y, logoBuffer(machines[logo_idx].logo()), (menu_sel-1) == logo_idx);
    }

    if(row == 35) {
      // finally scroll_offset is bound to game, something like 96 * game:
      int new_offset = 96 * ((unsigned)(menu_sel - 2) % machinesCount);
      if(menu_sel == 1)
        new_offset = (machinesCount - 1) * 96;

      // check if we need to scroll
      if(new_offset != scroll_offset) {
        int diff = (new_offset - scroll_offset) % (machinesCount * 96);
        if(diff < 0) diff += machinesCount * 96;
        if(diff < machinesCount * 96 / 2)
          scroll_offset = (scroll_offset + 8) % (machinesCount * 96);
        else
          scroll_offset = (scroll_offset - 8) % (machinesCount * 96);
        if(scroll_offset < 0)
          scroll_offset += machinesCount * 96;
      }
    }
  }

#if defined(MASTER_ATTRACT_MENU_TIMEOUT) && defined(MASTER_ATTRACT_MENU_SHOW_COUNTDOWN)
  if(row==0 && master_attract_timeout) {
    // elapsed can exceed the timeout for one frame at the deadline.
    // Signed math clamps to 0; unsigned would wrap.
    long elapsed = (long)(millis() - master_attract_timeout);
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
void Menu::menu_logo(short row, const unsigned short *img, char active) {
  // less than 8 rows in image left?
  unsigned short pix2draw = ((row <= 96 - 8) ? (224 * 8) : ((96 - row) * 224));

  unsigned short ipix = (row < 0) ? (unsigned short)(-row * 224) : 0;

  // Logo got no pool slot.
  if(!img) {
    while(ipix < pix2draw) { frame_buffer[ipix++] = 0; }
    return;
  }

  const unsigned short *src = img + 224 * (row >= 0 ? row : 0);

  while(ipix < pix2draw)
    frame_buffer[ipix++] = active ? *src++ : convert_RGB565_to_greyscale(*src++);
}

// Decode only logos visible this frame. Caching all ~50 does not fit in RAM.
void Menu::refreshLogoCache() {
  RomData<unsigned short, COMPRESSED> *needed[LOGO_CACHE_SIZE] = { };
  unsigned char needed_count = 0;

  auto addNeeded = [&](signed char idx) {
    RomData<unsigned short, COMPRESSED> *r = &machines[idx].logo();
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
const unsigned short *Menu::logoBuffer(RomData<unsigned short, COMPRESSED> &logo) {
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
    case MCH_CIRCUSC:       return "Circus Charlie";
    case MCH_ROCNROPE:      return "Roc'n Rope";
    case MCH_TODRUAGA:      return "Tower of Druaga";
    case MCH_VANVAN:        return "Van Van Car";
    case MCH_PBACTION:      return "Pinball Action";
    case MCH_MOTORACE:      return "Moto Race USA";
    case MCH_ROADFIGHTER:   return "Road Fighter";
    case MCH_FANTASY:       return "Fantasy";
    case MCH_NIBBLER:       return "Nibbler";
    case MCH_SCREGG:        return "Scrambled Egg";
    case MCH_VANGUARD:      return "Vanguard";
    case MCH_ZAXXON:        return "Zaxxon";
  }

  return "";
}
