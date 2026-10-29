#ifdef HAL_RP2350
#include "../../config.h"  // the board profile selects the backends
#if defined(HAL_GAMEPAD_BT) || defined(ASSETS_REMOTE)
#include "cyw43.h"
#include <stdio.h>
#include <pico/async_context_freertos.h>
#include <pico/cyw43_arch.h>
#include <task.h>
#ifdef ASSETS_REMOTE
#include <lwip/opt.h>  // TCPIP_THREAD_NAME
#endif
#include "rtos.h"

namespace hal {
namespace {

// above the main task (1): CYW43 work is short, the main task busy-waits on
// the beam
const UBaseType_t TASK_PRIORITY = 2;
const uint32_t TASK_STACK_BYTES = 4096;

async_context_freertos_t context;
bool tried;
bool ready;

} // namespace

async_context *cyw43Begin() {
  if (tried) {
    return ready ? &context.core : nullptr;
  }
  tried = true;
  async_context_freertos_config_t config = async_context_freertos_default_config();
  config.task_priority = TASK_PRIORITY;
  config.task_stack_size = TASK_STACK_BYTES / sizeof(StackType_t);
  config.task_core_id = VIDEO_CORE;
  if (!async_context_freertos_init(&context, &config)) {
    printf("CYW43: no task\n");
    return nullptr;
  }
  cyw43_arch_set_async_context(&context.core);
  if (cyw43_arch_init()) {
    printf("CYW43: init failed\n");
    return nullptr;
  }
#ifdef ASSETS_REMOTE
  // lwIP's tcpip thread (started by cyw43_arch_init) is unpinned: keep its
  // timers off the emulation core
  TaskHandle_t tcpip = xTaskGetHandle(TCPIP_THREAD_NAME);
  if (tcpip) {
    vTaskCoreAffinitySet(tcpip, 1u << VIDEO_CORE);
  }
#endif
  ready = true;
  return &context.core;
}

#ifndef HAL_GAMEPAD_BT
void btScan(BtScan) { }
#endif

} // namespace hal
#endif // HAL_GAMEPAD_BT || ASSETS_REMOTE
#endif // HAL_RP2350
