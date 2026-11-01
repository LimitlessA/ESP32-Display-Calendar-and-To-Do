#include "crashlog.h"

#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

#if __has_include(<esp_private/panic_internal.h>)
#include <esp_private/panic_internal.h>  // g_panic_abort_details
#define CRASHLOG_ABORT_DETAILS 1
#endif

namespace {

// ---- Texts and settings ------------------------------------------------------

constexpr uint32_t kMagic = 0x43524153;  // "CRAS": the record below is ours, not power-on noise
constexpr int kDepth = 10;               // backtrace entries kept
constexpr uint32_t kPollMs = 2000;
constexpr const char *kCrash = "Crash in %s (%s)";
constexpr const char *kRestart = "Restarted: %s%s";

// ---- State -------------------------------------------------------------------

// RTC memory keeps its contents over a restart (not over a power cut).
struct Record {
  uint32_t magic;
  uint32_t panic;       // 1 + panic_exception_t, 0 = no crash recorded
  uint32_t core;
  uint32_t pc[kDepth];
  char reason[48];
  char task[16];
  uint32_t heapLow;     // lowest free internal RAM seen (bytes)
  uint32_t uptime;      // seconds, updated with heapLow
};
RTC_NOINIT_ATTR Record rec;

Record last;  // the previous run's record, copied at start
bool haveLast = false;
esp_reset_reason_t resetReason = ESP_RST_UNKNOWN;
uint32_t lastPoll = 0;

// ---- Code --------------------------------------------------------------------

void copyText(char *dst, size_t size, const char *src) {
  size_t i = 0;
  for (; src && src[i] && i + 1 < size; i++) dst[i] = src[i];
  dst[i] = 0;
}

const char *reasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "power on";
    case ESP_RST_EXT: return "reset pin";
    case ESP_RST_SW: return "restart";
    case ESP_RST_PANIC: return "crash";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_BROWNOUT: return "power dip (brownout)";
    case ESP_RST_USB: return "USB";
    default: return "other";
  }
}

String format(const char *fmt, const char *a, const char *b = "") {
  char buf[96];
  snprintf(buf, sizeof(buf), fmt, a, b);
  return buf;
}

// The Arduino core's panic hook calls this with the backtrace already walked:
// note what happened, then ESP-IDF prints it and restarts as usual. Runs in
// the panic context, so it only copies a few values. ESP-IDF has turned the
// cache back on by then, so it need not be in IRAM (internal RAM too).
void onPanic(arduino_panic_info_t *info, void *) {
  rec.magic = kMagic;
  rec.panic = 1;
  rec.core = info->core;
  const char *why = info->reason;
#ifdef CRASHLOG_ABORT_DETAILS
  if (g_panic_abort_details) why = g_panic_abort_details;  // abort(): the assert text
#endif
  copyText(rec.reason, sizeof(rec.reason), why);
  TaskHandle_t task = xTaskGetCurrentTaskHandleForCore(info->core);
  copyText(rec.task, sizeof(rec.task), task ? pcTaskGetName(task) : "");
  for (int i = 0; i < kDepth; i++) rec.pc[i] = i < (int)info->backtrace_len ? info->backtrace[i] : 0;
}

}  // namespace

void crashlog_begin() {
  resetReason = esp_reset_reason();
  if (rec.magic == kMagic) {
    last = rec;
    haveLast = true;
    // A record from before a power cut or reset pin is stale.
    if (resetReason != ESP_RST_PANIC && resetReason != ESP_RST_INT_WDT && resetReason != ESP_RST_TASK_WDT &&
        resetReason != ESP_RST_WDT) {
      last.panic = 0;
    }
  }
  memset(&rec, 0, sizeof(rec));
  rec.magic = kMagic;
  set_arduino_panic_handler(onPanic, nullptr);
  rec.heapLow = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
  const String text = crashlog_details();
  Serial.println(text);
}

void crashlog_poll() {
  if (millis() - lastPoll < kPollMs) return;
  lastPoll = millis();
  rec.heapLow = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
  rec.uptime = millis() / 1000;
}

String crashlog_summary() {
  if (haveLast && last.panic) return format(kCrash, last.task[0] ? last.task : "?", last.reason);
  switch (resetReason) {
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
    case ESP_RST_BROWNOUT: return format(kRestart, reasonName(resetReason));
    default: return "";
  }
}

String crashlog_details() {
  String s = "Last reset: ";
  s += reasonName(resetReason);
  if (haveLast && last.panic) {
    s += "\nCrash: ";
    s += last.reason;
    s += " in task ";
    s += last.task;
    s += " on core ";
    s += last.core;
    s += "\nBacktrace:";
    char buf[12];
    for (uint32_t pc : last.pc) {
      if (!pc) break;
      snprintf(buf, sizeof(buf), " 0x%08lx", (unsigned long)pc);
      s += buf;
    }
  }
  if (haveLast) {
    s += "\nBefore it: up ";
    s += last.uptime;
    s += " s, internal RAM low ";
    s += last.heapLow;
    s += " bytes";
  }
  return s;
}
