#include <SD.h>
#include <SPI.h>

#include "config.h"
#include "storage.h"

namespace {

SemaphoreHandle_t lock;
bool mounted = false;

struct Guard {
  Guard() { xSemaphoreTake(lock, portMAX_DELAY); }
  ~Guard() { xSemaphoreGive(lock); }
};

void makeParentDirs(const char *path) {
  String p = path;
  for (int i = p.indexOf('/', 1); i > 0; i = p.indexOf('/', i + 1)) {
    const String dir = p.substring(0, i);
    if (!SD.exists(dir)) SD.mkdir(dir);
  }
}

}  // namespace

bool storage_begin() {
  lock = xSemaphoreCreateMutex();
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  mounted = SD.begin(SD_CS, SPI, 20000000);
  log_i("SD card %s", mounted ? "mounted" : "not found");
  return mounted;
}

bool storage_ok() {
  return mounted;
}

uint64_t storage_free_bytes() {
  if (!mounted) return 0;
  Guard g;
  return SD.totalBytes() - SD.usedBytes();
}

bool storage_write(const char *path, const String &data) {
  if (!mounted) return false;
  Guard g;
  makeParentDirs(path);
  // Write to a temp file first so a power cut never leaves half a file.
  const String tmp = String(path) + ".tmp";
  File f = SD.open(tmp, FILE_WRITE);
  if (!f) return false;
  const size_t n = f.print(data);
  f.close();
  if (n != data.length()) return false;
  SD.remove(path);
  return SD.rename(tmp, path);
}

bool storage_read(const char *path, String &out) {
  if (!mounted) return false;
  Guard g;
  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  out = f.readString();
  f.close();
  return true;
}
