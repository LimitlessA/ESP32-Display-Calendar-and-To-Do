#pragma once

#include <Arduino.h>

// TF card access. Every call is safe without a card (it just returns false)
// and safe from any task.
bool storage_begin();
bool storage_ok();
uint64_t storage_free_bytes();
bool storage_write(const char *path, const String &data);
bool storage_read(const char *path, String &out);
