// RGB panel bus for the ESP32-S3 with bounce buffers: LovyanGFX's Bus_RGB,
// but the LCD's DMA reads a ring of small buffers in internal RAM, which an
// interrupt refills from the PSRAM frame buffer. Reading the frame buffer
// straight from PSRAM (as Bus_RGB does) starves the LCD whenever the CPU or
// Wi-Fi also use PSRAM, so the picture jumps sideways; it also limits the
// pixel clock, so the panel refreshes slowly and flickers. ESP-IDF 5 does the
// same in its RGB driver; this core is ESP-IDF 4.4, which cannot.
#pragma once

#include <LovyanGFX.hpp>
#include <esp_async_memcpy.h>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

class BusRgbBounce : public lgfx::IBus {
public:
  using config_t = lgfx::Bus_RGB::config_t;

  // A ring of 4 buffers of 6 lines (4 x 9.6 KB): each refill has the time of
  // three buffers (0.7 ms at 24 MHz), so a copy slowed down by other PSRAM
  // traffic still lands in time. Refills use the DMA copier, not the CPU. The panel height must be a multiple of
  // kBounceBufs * kBounceLines.
  static constexpr int kBounceBufs = 4;
  static constexpr int kBounceLines = 6;

  const config_t &config() const { return _cfg; }
  void config(const config_t &cfg) { _cfg = cfg; }

  lgfx::bus_type_t busType() const override { return lgfx::bus_type_t::bus_unknown; }

  bool init() override;
  void release() override {}

  void beginTransaction() override {}
  void endTransaction() override {}
  void wait() override {}
  bool busy() const override { return false; }

  void flush() override {}
  bool writeCommand(uint32_t, uint_fast8_t) override { return true; }
  void writeData(uint32_t, uint_fast8_t) override {}
  void writeDataRepeat(uint32_t, uint_fast8_t, uint32_t) override {}
  void writePixels(lgfx::pixelcopy_t *, uint32_t) override {}
  void writeBytes(const uint8_t *, uint32_t, bool, bool) override {}

  void initDMA() override {}
  void addDMAQueue(const uint8_t *, uint32_t) override {}
  void execDMAQueue() override {}
  uint8_t *getDMABuffer(uint32_t) override { return _frame_buffer; }

  // The refills read the frame buffer with DMA, past the CPU cache: call
  // after drawing into lines y .. y + h - 1 so they see the new pixels.
  void writeBack(int y, int h);

  void beginRead() override {}
  void endRead() override {}
  uint32_t readData(uint_fast8_t) override { return 0; }
  bool readBytes(uint8_t *, uint32_t, bool) override { return false; }
  void readPixels(void *, lgfx::pixelcopy_t *, uint32_t) override {}

private:
  static void vsyncIsr(void *arg);
  static void eofIsr(void *arg);
  struct Refill {
    uint8_t *dst, *src;
  };
  static constexpr int kRefillSlots = 16;
  static void copyTask(void *arg);
  void fill(int buf, int chunk);
  void copy(const Refill &r);

  config_t _cfg;
  uint8_t *_frame_buffer = nullptr;  // whole frame, PSRAM
  uint8_t *_bounce[kBounceBufs] = {};  // internal RAM, DMA capable
  size_t _chunk_bytes = 0;           // bytes in one bounce buffer
  int _chunks = 0;                   // bounce buffers per frame
  dma_descriptor_t *_desc = nullptr;
  dma_descriptor_t _restart;
  int _dma_ch = -1;
  volatile int _next_buf = 0;    // buffer the DMA finishes next
  volatile int _next_chunk = 0;  // part of the frame to load into it
  esp_lcd_i80_bus_handle_t _i80_bus = nullptr;
  intr_handle_t _lcd_intr = nullptr;
  intr_handle_t _dma_intr = nullptr;
  async_memcpy_t _copier = nullptr;
  TaskHandle_t _copy_task = nullptr;
  Refill _refills[kRefillSlots] = {};  // queued by the interrupt for copyTask
  volatile int _refill_head = 0, _refill_tail = 0;
};
