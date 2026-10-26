#ifndef HAL_ENTRY_H
#define HAL_ENTRY_H

// Program entry. The application defines these (main.cpp); the entry backend
// calls them: app_main() (ESP-IDF) or main() (host, RP2350).
void galagino_setup(void);
void galagino_loop(void);

#endif // HAL_ENTRY_H
