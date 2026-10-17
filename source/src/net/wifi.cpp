// WiFi for the game loader (ASSETS_REMOTE builds): up, one HTTP GET
// streamed into an HttpBody, down. Only between game select and game ctor,
// never during gameplay. Every fix below was measured on the device.
//
// Why
//   No PSRAM. After Arena, the system heap has ~25 KB free, largest block
//   ~14 KB. The WiFi driver needs ~34 KB heap plus a ~6.6 KB task stack.
//   Stock esp_wifi_init does not fit without shrinking Arena for good.
//
// Problems solved
//   - Size: driver heap and task stack come from Arena, which is empty
//     between game select and game ctor. System heap use while WiFi is on
//     drops from ~9.6 KB to small queues/semaphores.
//   - Fragmentation: driver blocks left in the system heap split its
//     largest block. After ~20 sessions the task stack no longer fit:
//     "wifi:create wifi task: failed to create task".
//   - Leaks in IDF v4.4.7:
//     - key buffers, 2 x 184 B per session: freed here (keySlotsFree)
//     - SAE H2E, 88 B per connect: avoided with hunt-and-peck
//
// How
//   Arena:  [ wifiHeap (multi_heap) ]   during WiFi only
//
//   esp_wifi_init(cfg.osi_funcs = osi)
//     osi alloc hooks   -> wifiHeap, system heap if full (fallbacks)
//     osi task create   -> static task, stack + TCB in wifiHeap
//   free/realloc        -> --wrap at link time, routed by address:
//                          IDF frees across components (driver frees
//                          supplicant mallocs, plain free() of IPC msgs)
//   teardown            -> deinit, wait until tasks gone, free key slots,
//                          wifiHeap must be empty (else abort), Arena::reset
//
// free() routing (-Wl,--wrap=free)
//   The linker sends every reference to free, in all objects and
//   precompiled .a libs (the closed WiFi blobs too), to __wrap_free.
//   __real_free is the original newlib free. Not redirected: calls inside
//   the object that defines free (newlib itself, irrelevant here).
//
//   __wrap_free(p): p in wifiHeap ? multi_heap_free(wifiHeap, p)
//                                 : __real_free(p)
//
//   Arena blocks must go back to wifiHeap, not be ignored: IPC messages
//   are allocated all the time and teardown needs an empty wifiHeap.
//   Never call ::free() inside the wrapper: it loops back into it.
//   Needed because esp_wifi_ipc_internal and wifi_ipc_process free IPC
//   messages (allocated via our osi hook) with plain free().
//
// Build flag (platformio.ini): -Wl,--wrap=free
//   Missing flag fails the link: __real_free only exists with it.
//   Not wrapped: realloc, heap_caps_free/realloc. No WiFi caller passes
//   them an Arena pointer: the driver reallocs via osi hooks.
//
// Limits
//   - WiFi only while Arena is empty: never during a game.
//   - Pinned to IDF v4.4.7 blobs: key slot layout (g_ic+0x140, 25 slots),
//     osi table, task self-delete. Any IDF/Arduino core update: recheck.
//   - Not routed, stays in the system heap: netif, tcpip task, event loop
//     (~9.6 KB, once per boot), lwIP pbufs/sockets/timers, driver queues.
//   - Task wait counts all tasks: no other task may start or end while
//     WiFi is on.
//   - --wrap=free costs an address compare on every free, firmware wide.
//   - Owner tracking (caller per block) only with WIFI_HEAP_DEBUG.
#ifdef ASSETS_REMOTE
#include <Arduino.h>
#include <cstring>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_wifi_default.h>
#include <esp_event.h>
#include <esp_heap_caps.h>
#include <esp_private/wifi_os_adapter.h>
#include <multi_heap.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include "../config.h"
#include "../emulation/arena.h"
#include "http_body.h"
#include "wifi.h"

static constexpr uint32_t CONNECT_TIMEOUT_MS = 10000;
static constexpr uint16_t HTTP_DEFAULT_PORT = 80;
static constexpr uint32_t RECV_TIMEOUT_S = 5;

// Size: driver peak ~33.5 KB + WiFi task stack + TCB
static constexpr uint32_t WIFI_HEAP_BYTES = 49152;

static wifi_osi_funcs_t osi;
static multi_heap_handle_t wifiHeap = nullptr;
static portMUX_TYPE wifiHeapMux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t *heapStart = nullptr;
static uint8_t *heapEnd = nullptr;
static volatile uint32_t fallbacks = 0;  // allocations the Arena heap could not serve

extern "C" void __real_free(void *p);

static IRAM_ATTR bool inWifiHeap(const void *p) {
  return (const uint8_t *)p >= heapStart && (const uint8_t *)p < heapEnd;
}

#ifdef WIFI_HEAP_DEBUG
// WIFI_HEAP_DEBUG: caller of each live Arena heap block, printed when blocks
// survive deinit (resolve with addr2line on the .elf)
static constexpr uint32_t OWNERS_MAX = 128;

struct Owner {
  void *p;
  void *caller;
};

static Owner owners[OWNERS_MAX];
static volatile uint32_t ownersLost = 0;

static IRAM_ATTR void ownerSet(void *p, void *caller) {
  portENTER_CRITICAL_SAFE(&wifiHeapMux);
  uint32_t i = 0;
  while (i < OWNERS_MAX && owners[i].p) {
    i++;
  }
  if (i < OWNERS_MAX) {
    owners[i] = {p, caller};
  } else {
    ownersLost++;
  }
  portEXIT_CRITICAL_SAFE(&wifiHeapMux);
}

static IRAM_ATTR void ownerClear(void *p) {
  portENTER_CRITICAL_SAFE(&wifiHeapMux);
  for (uint32_t i = 0; i < OWNERS_MAX; i++) {
    if (owners[i].p == p) {
      owners[i].p = nullptr;
      break;
    }
  }
  portEXIT_CRITICAL_SAFE(&wifiHeapMux);
}
#else
static inline void ownerSet(void *, void *) { }
static inline void ownerClear(void *) { }
#endif

static IRAM_ATTR void heapFree(void *p) {
  ownerClear(p);
  multi_heap_free(wifiHeap, p);
}

extern "C" IRAM_ATTR void __wrap_free(void *p) {
  if (inWifiHeap(p)) {
    heapFree(p);
    return;
  }
  __real_free(p);
}

// ppInstallKey's key buffers (168 B + key) sit in g_ic key slots. Deinit
// keeps them: cnx_sta_leave frees only some slots, a later install frees
// only the slot it reuses. Leaks 2 x 184 B per session. Freed here after
// deinit. Layout from ieee80211_set_key, IDF v4.4.7 libnet80211.
extern "C" uint8_t g_ic[];
static constexpr uint32_t IC_KEY_SLOTS_OFFSET = 0x140;
static constexpr uint32_t IC_KEY_SLOTS = 25;

static void keySlotsFree() {
  void **slots = (void **)(g_ic + IC_KEY_SLOTS_OFFSET);
  for (uint32_t i = 0; i < IC_KEY_SLOTS; i++) {
    free(slots[i]);
    slots[i] = nullptr;
  }
}

// Arena memory is internal DRAM: DMA-capable, like _malloc_internal's
static IRAM_ATTR void *heapAlloc(size_t n, void *caller) {
  void *p = wifiHeap ? multi_heap_malloc(wifiHeap, n) : nullptr;
  if (p) {
    ownerSet(p, caller);
    return p;
  }
  if (wifiHeap) {
    fallbacks++;
  }
  return g_wifi_osi_funcs._malloc_internal(n);
}

static IRAM_ATTR void *heapZalloc(size_t n, void *caller) {
  void *p = heapAlloc(n, caller);
  if (p) {
    memset(p, 0, n);
  }
  return p;
}

// Arena heap full: move the block to the system heap
static IRAM_ATTR void *reallocWifiHeap(void *p, size_t n, void *caller) {
  ownerClear(p);
  void *q = multi_heap_realloc(wifiHeap, p, n);
  if (q) {
    ownerSet(q, caller);
    return q;
  }
  if (!n) {
    return q;
  }
  fallbacks++;
  q = g_wifi_osi_funcs._malloc_internal(n);
  if (q) {
    const size_t old = multi_heap_get_allocated_size(wifiHeap, p);
    memcpy(q, p, old < n ? old : n);
    multi_heap_free(wifiHeap, p);
  } else {
    ownerSet(p, caller);  // realloc failed: old block stays
  }
  return q;
}

#define CALLER __builtin_return_address(0)

// pointers from before the Arena heap, or fallbacks, keep the default realloc
static IRAM_ATTR void *wReallocInt(void *p, size_t n) {
  if (!p) {
    return heapAlloc(n, CALLER);
  }
  return inWifiHeap(p) ? reallocWifiHeap(p, n, CALLER) : g_wifi_osi_funcs._realloc_internal(p, n);
}

static IRAM_ATTR void *wRealloc(void *p, size_t n) {
  if (!p) {
    return heapAlloc(n, CALLER);
  }
  return inWifiHeap(p) ? reallocWifiHeap(p, n, CALLER) : g_wifi_osi_funcs._wifi_realloc(p, n);
}

// WiFi task stack + TCB in the Arena heap (static task), so the system heap
// needs no big contiguous block. The task deletes itself on deinit
// (pp_delete_task), the idle task then still touches its TCB: memory is
// freed only after the task count drops back (tasksGone).
static constexpr uint32_t WIFI_TASKS_MAX = 4;
static constexpr uint32_t TASK_EXIT_TIMEOUT_MS = 1000;
static constexpr uint32_t TASK_EXIT_POLL_MS = 10;

struct TaskMem {
  StaticTask_t *tcb;
  StackType_t *stack;
};

static TaskMem taskMem[WIFI_TASKS_MAX];
static UBaseType_t tasksBefore = 0;

static int32_t wTaskCreatePinned(void *fn, const char *name, uint32_t stackBytes, void *param, uint32_t prio,
                                 void *handle, uint32_t core) {
  uint32_t i = 0;
  while (i < WIFI_TASKS_MAX && taskMem[i].tcb) {
    i++;
  }
  if (i == WIFI_TASKS_MAX) {
    return g_wifi_osi_funcs._task_create_pinned_to_core(fn, name, stackBytes, param, prio, handle, core);
  }

  // IDF stack depth is in bytes
  StaticTask_t *tcb = (StaticTask_t *)heapAlloc(sizeof(StaticTask_t), CALLER);
  StackType_t *stack = (StackType_t *)heapAlloc(stackBytes, CALLER);
  TaskHandle_t t = nullptr;
  if (tcb && stack) {
    t = xTaskCreateStaticPinnedToCore((TaskFunction_t)fn, name, stackBytes, param, prio, stack, tcb,
                                      core < portNUM_PROCESSORS ? core : tskNO_AFFINITY);
  }
  if (!t) {
    free(tcb);
    free(stack);
    return pdFAIL;
  }

  taskMem[i] = {tcb, stack};
  if (handle) {
    *(TaskHandle_t *)handle = t;
  }
  return pdPASS;
}

static int32_t wTaskCreate(void *fn, const char *name, uint32_t stackBytes, void *param, uint32_t prio,
                           void *handle) {
  return wTaskCreatePinned(fn, name, stackBytes, param, prio, handle, tskNO_AFFINITY);
}

// idle task done with the deleted WiFi tasks' TCBs
static bool tasksGone() {
  for (uint32_t ms = 0; ms < TASK_EXIT_TIMEOUT_MS; ms += TASK_EXIT_POLL_MS) {
    const bool gone = uxTaskGetNumberOfTasks() <= tasksBefore;
    // count drops right before prvDeleteTCB: one more poll lets it finish
    vTaskDelay(pdMS_TO_TICKS(TASK_EXIT_POLL_MS));
    if (gone) {
      return true;
    }
  }
  return false;
}

static void taskMemFree() {
  if (!tasksGone()) {
    printf("wifi: WiFi tasks still alive after deinit\n");
    abort();
  }
  for (uint32_t i = 0; i < WIFI_TASKS_MAX; i++) {
    free(taskMem[i].tcb);
    free(taskMem[i].stack);
    taskMem[i] = {};
  }
}

static IRAM_ATTR void *wMalloc(size_t n) { return heapAlloc(n, CALLER); }
static IRAM_ATTR void *wMalloc32(unsigned int n) { return heapAlloc(n, CALLER); }
static IRAM_ATTR void *wZalloc(size_t n) { return heapZalloc(n, CALLER); }
static IRAM_ATTR void *wCalloc(size_t n, size_t s) { return heapZalloc(n * s, CALLER); }
static IRAM_ATTR void wFree(void *p) { free(p); }

static void heapLog(const char *step) {
  multi_heap_info_t info = {};
  if (wifiHeap) {
    multi_heap_get_info(wifiHeap, &info);
  }
  printf("wifi: %-10s Free=%u MaxAlloc=%u MinFree=%u Arena heap now=%u peak=%u fallbacks=%u\n", step,
         heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
         heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT), info.total_allocated_bytes,
         wifiHeap ? WIFI_HEAP_BYTES - info.minimum_free_bytes : 0, fallbacks);
}

static void heapOpen() {
  heapStart = Arena::alloc(WIFI_HEAP_BYTES, 4);
  heapEnd = heapStart + WIFI_HEAP_BYTES;
  wifiHeap = multi_heap_register(heapStart, WIFI_HEAP_BYTES);
  multi_heap_set_lock(wifiHeap, &wifiHeapMux);  // portMUX is recursive, as heap_caps uses it
}

// the game unpacks into this memory next: nothing may stay allocated
static void heapClose() {
  multi_heap_info_t info;
  multi_heap_get_info(wifiHeap, &info);
  if (info.allocated_blocks) {
    printf("wifi: Arena heap not empty after deinit: %u blocks, %u bytes\n",
           info.allocated_blocks, info.total_allocated_bytes);
#ifdef WIFI_HEAP_DEBUG
    printf("wifi: owners lost %u\n", ownersLost);
    for (uint32_t i = 0; i < OWNERS_MAX; i++) {
      if (owners[i].p) {
        printf("wifi:   %p %u bytes from %p\n", owners[i].p,
               multi_heap_get_allocated_size(wifiHeap, owners[i].p), owners[i].caller);
      }
    }
#endif
    abort();
  }
  wifiHeap = nullptr;
  heapStart = heapEnd = nullptr;
  Arena::reset();
}

static volatile bool gotIp = false;

static void onEvent(void *, esp_event_base_t base, int32_t id, void *) {
  if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    gotIp = true;
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  }
}

// persistent across game starts: tcpip task, event loop, netif
static void initOnce() {
  static bool done = false;
  if (done) {
    return;
  }
  done = true;
  esp_netif_init();
  esp_event_loop_create_default();
  esp_netif_create_default_wifi_sta();
  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, onEvent, nullptr);
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, onEvent, nullptr);
}

static bool wifiUp() {
  osi = g_wifi_osi_funcs;
  osi._malloc = wMalloc32;
  osi._free = wFree;
  osi._malloc_internal = wMalloc;
  osi._realloc_internal = wReallocInt;
  osi._calloc_internal = wCalloc;
  osi._zalloc_internal = wZalloc;
  osi._wifi_malloc = wMalloc;
  osi._wifi_realloc = wRealloc;
  osi._wifi_calloc = wCalloc;
  osi._wifi_zalloc = wZalloc;
  osi._task_create_pinned_to_core = wTaskCreatePinned;
  osi._task_create = wTaskCreate;
  tasksBefore = uxTaskGetNumberOfTasks();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  cfg.osi_funcs = &osi;
  cfg.nvs_enable = 0;  // no flash writes on connect
  // Arduino's values: IDF defaults reserve 8 static TX + 8 RX buffers at
  // init (~1.6 KB each) and run out of heap
  cfg.static_tx_buf_num = 0;
  cfg.dynamic_tx_buf_num = 32;
  cfg.tx_buf_type = 1;
  cfg.cache_tx_buf_num = 4;
  cfg.static_rx_buf_num = 4;
  cfg.dynamic_rx_buf_num = 32;
  if (esp_wifi_init(&cfg) != ESP_OK) {
    printf("wifi: esp_wifi_init failed\n");
    return false;
  }

  wifi_config_t sta = {};
  strncpy((char *)sta.sta.ssid, GALAGINO_WIFI_SSID, sizeof(sta.sta.ssid));
  strncpy((char *)sta.sta.password, GALAGINO_WIFI_PASS, sizeof(sta.sta.password));
  // IDF 4.4.7 SAE H2E (sswu) leaks 88 B per connect, fixed in 4.4.8 (509c4c2)
  sta.sta.sae_pwe_h2e = WPA3_SAE_PWE_HUNT_AND_PECK;
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_set_config(WIFI_IF_STA, &sta);

  gotIp = false;
  const uint32_t t0 = millis();
  esp_wifi_start();
  while (!gotIp && millis() - t0 < CONNECT_TIMEOUT_MS) {
    delay(50);
  }
  if (!gotIp) {
    printf("wifi: connect timeout\n");
    return false;
  }
  printf("wifi: connected in %u ms\n", millis() - t0);
  return true;
}

// url: http://a.b.c.d[:port]/path (no DNS)
bool Wifi::get(const char *url, HttpBody &body) {
  static const char PREFIX[] = "http://";
  if (strncmp(url, PREFIX, sizeof(PREFIX) - 1)) {
    printf("wifi: bad url %s\n", url);
    return false;
  }
  char host[64];
  const char *h = url + sizeof(PREFIX) - 1;
  const char *slash = strchr(h, '/');
  const char *path = slash ? slash : "/";
  const size_t hostLen = slash ? slash - h : strlen(h);
  snprintf(host, sizeof(host), "%.*s", (int)hostLen, h);
  uint16_t port = HTTP_DEFAULT_PORT;
  char *colon = strchr(host, ':');
  if (colon) {
    *colon = 0;
    port = atoi(colon + 1);
  }

  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (!inet_aton(host, &addr.sin_addr)) {
    printf("wifi: host must be an IP: %s\n", host);
    return false;
  }

  const int s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0 || connect(s, (sockaddr *)&addr, sizeof(addr))) {
    printf("wifi: tcp connect failed\n");
    if (s >= 0) {
      close(s);
    }
    return false;
  }

  // a stalled server ends the download instead of hanging the loader
  const timeval timeout = {RECV_TIMEOUT_S, 0};
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  char req[160];
  const int n = snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n", path, host);
  send(s, req, n, 0);

  // from the Arena heap: static DRAM is too tight (solomon low-water 2.9 KB)
  const size_t BUF_BYTES = 4096;
  uint8_t *buf = (uint8_t *)multi_heap_malloc(wifiHeap, BUF_BYTES);
  if (!buf) {
    printf("wifi: no receive buffer\n");
    close(s);
    return false;
  }
  bool ok = true;
  int r;
  while (ok && (r = recv(s, buf, BUF_BYTES, 0)) > 0) {
    ok = body.feed(buf, r);
  }
  multi_heap_free(wifiHeap, buf);
  close(s);
  return ok && body.complete();
}

bool Wifi::up() {
  initOnce();
  heapOpen();
  return wifiUp();
}

void Wifi::down() {
  esp_wifi_disconnect();
  esp_wifi_stop();
  esp_wifi_deinit();
  taskMemFree();
  keySlotsFree();
  heapLog("down");
  heapClose();
}
#endif
