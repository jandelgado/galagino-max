// Program entry of the ESP-IDF only build. The main task runs on core 0
// (CONFIG_ESP_MAIN_TASK_AFFINITY_CPU0), the video core.
#ifndef ARDUINO
#include "../entry.h"

extern "C" void app_main(void) {
  galagino_setup();
  for (;;) {
    galagino_loop();
  }
}
#endif
