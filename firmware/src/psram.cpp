#include "psram.h"

#include <esp_heap_caps.h>
#include <esp_memory_utils.h>

namespace {

constexpr uint32_t kPsram = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;

struct PsramJson : ArduinoJson::Allocator {
  void *allocate(size_t size) override {
    void *p = heap_caps_malloc(size, kPsram);
    return p ? p : malloc(size);
  }
  void deallocate(void *p) override { free(p); }
  void *reallocate(void *p, size_t size) override {
    void *q = heap_caps_realloc(p, size, kPsram);
    return q ? q : realloc(p, size);
  }
};

PsramJson jsonAllocator;

}  // namespace

bool psram_task(TaskFunction_t fn, const char *name, uint32_t stackBytes, void *arg, UBaseType_t priority,
                TaskHandle_t *handle, BaseType_t core) {
#if CONFIG_SPIRAM && CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM
  if (xTaskCreatePinnedToCoreWithCaps(fn, name, stackBytes, arg, priority, handle, core, kPsram) == pdPASS) return true;
#else
#warning "Task stacks cannot go to PSRAM in this build (CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM)"
#endif
  return xTaskCreatePinnedToCore(fn, name, stackBytes, arg, priority, handle, core) == pdPASS;
}

ArduinoJson::Allocator *psram_json() { return &jsonAllocator; }

String psram_task_report() {
#if configUSE_TRACE_FACILITY
  UBaseType_t n = uxTaskGetNumberOfTasks() + 4;
  auto *tasks = static_cast<TaskStatus_t *>(heap_caps_malloc(n * sizeof(TaskStatus_t), kPsram));
  if (!tasks) return "out of memory";
  n = uxTaskGetSystemState(tasks, n, nullptr);
  String out;
  for (UBaseType_t i = 0; i < n; i++) {
    out += tasks[i].pcTaskName;
    out += ' ';
    out += (unsigned)tasks[i].usStackHighWaterMark;
    if (esp_ptr_external_ram(tasks[i].pxStackBase)) out += " psram";
    out += '\n';
  }
  heap_caps_free(tasks);
  return out;
#else
  return "needs CONFIG_FREERTOS_USE_TRACE_FACILITY";
#endif
}
