// LVGL heap for the ESP32-S3: every UI object goes to PSRAM. With plain
// malloc, allocations under 4 KB stay in internal RAM, and the thousands of
// small objects the screens create used it up, so Wi-Fi could no longer start
// a scan and the board rebooted in a loop. Wi-Fi, TLS and DMA need internal
// RAM; LVGL does not.
#pragma once

#include <stdlib.h>

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>

static inline void *lv_psram_alloc(size_t size) {
  void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : malloc(size);
}

static inline void *lv_psram_realloc(void *ptr, size_t size) {
  void *p = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return (p || size == 0) ? p : realloc(ptr, size);
}
#else
#define lv_psram_alloc malloc
#define lv_psram_realloc realloc
#endif
