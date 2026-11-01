#include "harness.h"

#if HARNESS

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <lvgl.h>
#include <stdarg.h>

#include "display.h"
#include "web.h"
#include "ui_internal.h"

namespace {

// ---- Touch the harness is pressing ----
struct Gesture {
  volatile bool active = false;
  uint32_t start = 0, duration = 0;
  int16_t x1 = 0, y1 = 0, x2 = 0, y2 = 0;
} gesture;

void startGesture(int x1, int y1, int x2, int y2, uint32_t ms) {
  gesture.x1 = x1;
  gesture.y1 = y1;
  gesture.x2 = x2;
  gesture.y2 = y2;
  gesture.duration = ms;
  gesture.start = millis();
  gesture.active = true;
}

// ---- Recent log lines, kept in PSRAM ----
constexpr size_t kLogSize = 8192;
char *logRing = nullptr;
size_t logHead = 0;
bool logWrapped = false;
vprintf_like_t originalVprintf = nullptr;

int logHook(const char *fmt, va_list args) {
  char line[256];
  va_list copy;
  va_copy(copy, args);
  const int n = vsnprintf(line, sizeof(line), fmt, copy);
  va_end(copy);
  if (logRing && n > 0) {
    for (int i = 0; i < n && i < (int)sizeof(line) - 1; i++) {
      logRing[logHead++] = line[i];
      if (logHead == kLogSize) {
        logHead = 0;
        logWrapped = true;
      }
    }
  }
  return originalVprintf ? originalVprintf(fmt, args) : vprintf(fmt, args);
}

String recentLog() {
  if (!logRing) return "";
  String out;
  if (logWrapped) {
    for (size_t i = logHead; i < kLogSize; i++) out += logRing[i];
  }
  for (size_t i = 0; i < logHead; i++) out += logRing[i];
  return out;
}

// ---- Commands from the web server, run on the UI task ----
struct Request {
  String cmd;
  String reply;
  SemaphoreHandle_t done;
};
QueueHandle_t requests = nullptr;

// ---- Screenshot over serial: run-length pairs (count, colour), base64 ----
const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void printBase64(const uint8_t *data, size_t len) {
  char line[77];
  for (size_t off = 0; off < len; off += 57) {
    const size_t n = std::min<size_t>(57, len - off);
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
      const uint32_t v = data[off + i] << 16 | (i + 1 < n ? data[off + i + 1] << 8 : 0) |
                         (i + 2 < n ? data[off + i + 2] : 0);
      line[o++] = kB64[v >> 18 & 63];
      line[o++] = kB64[v >> 12 & 63];
      line[o++] = i + 1 < n ? kB64[v >> 6 & 63] : '=';
      line[o++] = i + 2 < n ? kB64[v & 63] : '=';
    }
    line[o] = 0;
    Serial.println(line);
  }
}

String serialShot() {
  int w, h;
  const uint16_t *fb = display_frame(&w, &h);
  if (!fb) return "no frame buffer";
  const size_t px = (size_t)w * h;
  auto *rle = (uint16_t *)heap_caps_malloc(px * 4, MALLOC_CAP_SPIRAM);
  if (!rle) return "out of memory";
  size_t pairs = 0;
  for (size_t i = 0; i < px;) {
    const uint16_t c = fb[i];
    size_t run = 1;
    while (i + run < px && fb[i + run] == c && run < 65535) run++;
    rle[pairs * 2] = run;
    rle[pairs * 2 + 1] = c;
    pairs++;
    i += run;
  }
  Serial.printf("SHOT %d %d %u\n", w, h, (unsigned)(pairs * 4));
  printBase64((const uint8_t *)rle, pairs * 4);
  Serial.println("SHOT END");
  heap_caps_free(rle);
  return "ok";
}

String heapReport() {
  return strf("internal free %u largest %u min %u | psram free %u largest %u",
              heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
              heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
              heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
              heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
              heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
}

const char *const kScreenNames[] = {"home", "detail", "week", "tasks", "night", "wifi",
                                    "accounts", "settings", "features", "dates", "desk"};
static_assert(sizeof(kScreenNames) / sizeof(kScreenNames[0]) == (int)Screen::Count,
              "one name per screen");

// Runs one command; over serial, `shot` prints the picture itself.
String execute(const String &line, bool overSerial) {
  char cmd[16] = {0};
  int a = 0, b = 0, c = 0, d = 0, e = 0;
  const int n = sscanf(line.c_str(), "%15s %d %d %d %d %d", cmd, &a, &b, &c, &d, &e);
  if (n < 1) return "";
  const String name = cmd;
  if (name == "tap" && n >= 3) {
    startGesture(a, b, a, b, 80);
    return "ok";
  }
  if (name == "press" && n >= 4) {
    startGesture(a, b, a, b, c);
    return "ok";
  }
  if (name == "swipe" && n >= 5) {
    startGesture(a, b, c, d, n >= 6 ? e : 300);
    return "ok";
  }
  if (name == "screen") {
    char which[16] = {0};
    sscanf(line.c_str(), "%*s %15s", which);
    for (int i = 0; i < (int)Screen::Count; i++) {
      if (strcmp(which, kScreenNames[i]) == 0) {
        ui_show((Screen)i);
        return "ok";
      }
    }
    String names;
    for (const char *s : kScreenNames) names += String(s) + " ";
    return "screens: " + names;
  }
  if (name == "shot") return overSerial ? serialShot() : "use /shot.bmp";
  if (name == "heap") return heapReport();
  // Serial only: whoever has the USB cable can already reflash the display.
  if (name == "pin") return overSerial ? web_pin() + " " + web_url() : "serial only";
  if (name == "log") return recentLog();
  if (name == "help") return "tap X Y | press X Y MS | swipe X1 Y1 X2 Y2 [MS] | screen NAME | shot | heap | log | pin";
  return "unknown command (help)";
}

String serialLine;

}  // namespace

void harness_begin() {
  logRing = (char *)heap_caps_calloc(1, kLogSize, MALLOC_CAP_SPIRAM);
  originalVprintf = esp_log_set_vprintf(logHook);
  requests = xQueueCreate(4, sizeof(Request *));
  Serial.println("harness: ready (type help)");
}

void harness_poll() {
  while (Serial.available()) {
    const char ch = Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (serialLine.length()) {
        const String reply = execute(serialLine, true);
        Serial.print("H> ");
        Serial.println(reply);
        serialLine = "";
      }
    } else if (serialLine.length() < 80) {
      serialLine += ch;
    }
  }
  Request *req;
  while (requests && xQueueReceive(requests, &req, 0) == pdTRUE) {
    req->reply = execute(req->cmd, false);
    xSemaphoreGive(req->done);
  }
}

bool harness_touch(int16_t *x, int16_t *y) {
  if (!gesture.active) return false;
  const uint32_t t = millis() - gesture.start;
  if (t >= gesture.duration) {
    gesture.active = false;  // release on the next read
    return false;
  }
  *x = gesture.x1 + (int32_t)(gesture.x2 - gesture.x1) * (int32_t)t / (int32_t)gesture.duration;
  *y = gesture.y1 + (int32_t)(gesture.y2 - gesture.y1) * (int32_t)t / (int32_t)gesture.duration;
  return true;
}

String harness_run(const String &cmd, uint32_t timeout_ms) {
  if (!requests) return "harness not started";
  Request req{cmd, "", xSemaphoreCreateBinary()};
  Request *p = &req;
  String reply = "timeout";
  if (xQueueSend(requests, &p, pdMS_TO_TICKS(100)) == pdTRUE &&
      xSemaphoreTake(req.done, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
    reply = req.reply;
  }
  vSemaphoreDelete(req.done);
  return reply;
}

void harness_send_bmp(WebServer &server) {
  int w, h;
  const uint16_t *fb = display_frame(&w, &h);
  if (!fb) {
    server.send(500, "text/plain", "no frame buffer");
    return;
  }
  // 16-bit BMP with RGB565 bit masks, rows bottom-up.
  const uint32_t row = w * 2, data = row * h, offset = 14 + 40 + 12;
  uint8_t head[66] = {'B', 'M'};
  auto put32 = [&](int at, uint32_t v) { memcpy(head + at, &v, 4); };
  auto put16 = [&](int at, uint16_t v) { memcpy(head + at, &v, 2); };
  put32(2, offset + data);
  put32(10, offset);
  put32(14, 40);
  put32(18, w);
  put32(22, h);
  put16(26, 1);
  put16(28, 16);
  put32(30, 3);  // BI_BITFIELDS
  put32(34, data);
  put32(54, 0xF800);
  put32(58, 0x07E0);
  put32(62, 0x001F);
  server.setContentLength(offset + data);
  server.send(200, "image/bmp", "");
  server.sendContent((const char *)head, sizeof(head));
  for (int y = h - 1; y >= 0; y--) server.sendContent((const char *)(fb + (size_t)y * w), row);
}

#else

void harness_begin() {}
void harness_poll() {}
bool harness_touch(int16_t *, int16_t *) { return false; }
String harness_run(const String &, uint32_t) { return "harness not built"; }
void harness_send_bmp(WebServer &server) { server.send(404, "text/plain", "harness not built"); }

#endif
