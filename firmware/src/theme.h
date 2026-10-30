#pragma once

#include <lvgl.h>

struct Theme {
  const char *name;
  bool dark;
  uint32_t bg;        // screen background
  uint32_t header;    // top bar
  uint32_t panel;     // large containers
  uint32_t card;      // cards and rows
  uint32_t raised;    // buttons, active tab
  uint32_t line;      // outlines, separators, inactive switch
  uint32_t text;
  uint32_t muted;     // secondary text (4.5:1 on card)
  uint32_t accent;    // highlights, primary buttons, "now"
  uint32_t onAccent;  // text on accent
  uint32_t good;      // done, synced
  uint32_t warnFg, warnBg;  // due today
  uint32_t badFg, badBg;    // overdue, errors
  uint32_t nightText;       // night clock, kept dim
};

extern const Theme kThemes[];
extern const uint8_t kThemeCount;

const Theme &theme();
void theme_select(uint8_t index);

inline lv_color_t c(uint32_t rgb) { return lv_color_hex(rgb); }
