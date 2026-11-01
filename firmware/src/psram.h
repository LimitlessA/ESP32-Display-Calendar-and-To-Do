#pragma once

// Keeping internal RAM free. The ESP32-S3 has about 300 KB of internal RAM
// once the program is loaded; the LCD's bounce buffers, LVGL's draw buffer,
// Wi-Fi, TLS sockets and Bluetooth must live there, so everything that can
// go to PSRAM (8 MB) goes there:
// - malloc()/new/String prefer PSRAM (main.cpp, heap_caps_malloc_extmem_enable),
// - JSON documents use psram_json(),
// - tasks that never need the flash cache off get their stack in PSRAM.

#include <Arduino.h>
#include <ArduinoJson.h>

// Starts a task with its stack in PSRAM (falls back to internal RAM if the
// build does not allow that). Only for tasks that do not write to flash
// themselves while some flash is mapped: with the program running from
// PSRAM (CONFIG_SPIRAM_XIP_FROM_PSRAM) that is any task here except the UI
// loop. Stack size in bytes.
bool psram_task(TaskFunction_t fn, const char *name, uint32_t stackBytes, void *arg, UBaseType_t priority,
                TaskHandle_t *handle = nullptr, BaseType_t core = 0);

// For JsonDocument doc(psram_json()): the document's pools and strings in PSRAM.
ArduinoJson::Allocator *psram_json();

// One line per task: name, free stack bytes at its fullest so far, "psram"
// if the stack is in PSRAM. For the test harness ("tasks"), to tune stacks.
String psram_task_report();
