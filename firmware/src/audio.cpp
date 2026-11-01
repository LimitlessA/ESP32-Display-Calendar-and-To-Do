#include <driver/i2s.h>
#include <math.h>

#include "audio.h"
#include "config.h"
#include "psram.h"

namespace {

constexpr i2s_port_t kPort = I2S_NUM_0;
constexpr int kRate = 16000;

struct Note {
  float freq;  // Hz
  uint16_t ms;
};

// Sounds: change the notes here
const Note kChime[] = {{880.0f, 220}, {1318.5f, 420}};  // meeting reminder: A5 then E6
const Note kAlert[] = {{987.8f, 160}, {987.8f, 160}, {1318.5f, 420}};  // leave-by: B5 B5 E6
const Note kDone[] = {{1046.5f, 160}, {1318.5f, 160}, {1568.0f, 460}};  // focus timer over: C6 E6 G6
const Note kTick[] = {{2000.0f, 18}};
constexpr float kMaxGain = 0.6f;  // loudness at 100% volume

struct Sound {
  const Note *notes;
  uint8_t count;
  uint8_t volume;
};

QueueHandle_t queue;

void playNote(const Note &n, float gain) {
  int16_t buf[256];
  const int total = kRate * n.ms / 1000;
  const float step = 2.0f * PI * n.freq / kRate;
  float phase = 0;
  for (int done = 0; done < total;) {
    const int chunk = min<int>(256, total - done);
    for (int i = 0; i < chunk; i++) {
      // Quick attack, exponential decay: sounds like a soft bell, not a beep.
      const float t = float(done + i) / total;
      const float env = min(1.0f, t * 40.0f) * expf(-4.0f * t);
      buf[i] = int16_t(sinf(phase) * env * gain * 32767.0f);
      phase += step;
    }
    size_t written;
    i2s_write(kPort, buf, chunk * sizeof(int16_t), &written, portMAX_DELAY);
    done += chunk;
  }
}

void audioTask(void *) {
  Sound s;
  for (;;) {
    if (xQueueReceive(queue, &s, portMAX_DELAY) != pdTRUE) continue;
    // Perceived loudness is roughly logarithmic, so square the slider value.
    const float gain = kMaxGain * (s.volume / 100.0f) * (s.volume / 100.0f);
    for (uint8_t i = 0; i < s.count; i++) playNote(s.notes[i], gain);
    i2s_zero_dma_buffer(kPort);
  }
}

}  // namespace

void audio_begin() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = kRate;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 4;
  cfg.dma_buf_len = 256;
  cfg.tx_desc_auto_clear = true;

  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = I2S_BCLK;
  pins.ws_io_num = I2S_LRCK;
  pins.data_out_num = I2S_DOUT;
  pins.data_in_num = I2S_PIN_NO_CHANGE;

  if (i2s_driver_install(kPort, &cfg, 0, nullptr) != ESP_OK || i2s_set_pin(kPort, &pins) != ESP_OK) {
    log_e("I2S init failed");
    return;
  }
  queue = xQueueCreate(4, sizeof(Sound));
  psram_task(audioTask, "audio", 4096, nullptr, 2, nullptr, 0);  // stack in PSRAM: 4 KB of internal RAM saved
}

void audio_chime(uint8_t volume) {
  if (!queue || volume == 0) return;
  Sound s = {kChime, 2, volume};
  xQueueSend(queue, &s, 0);
}

void audio_alert(uint8_t volume) {
  if (!queue || volume == 0) return;
  Sound s = {kAlert, 3, volume};
  xQueueSend(queue, &s, 0);
}

void audio_done(uint8_t volume) {
  if (!queue || volume == 0) return;
  Sound s = {kDone, 3, volume};
  xQueueSend(queue, &s, 0);
}

void audio_tick(uint8_t volume) {
  if (!queue || volume == 0) return;
  Sound s = {kTick, 1, volume};
  xQueueSend(queue, &s, 0);
}
