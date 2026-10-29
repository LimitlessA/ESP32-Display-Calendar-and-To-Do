// Settings > Features: a switch for every feature, in three layouts (mockup
// row 9: A list, B tabs, C tiles), plus the layout picker, which works like
// the theme picker: every screen that comes in A, B and C can be switched.
#include "sensors.h"
#include "ui_internal.h"

namespace {

// ---- Texts and settings ------------------------------------------------------

constexpr const char *kTitle = "Features";
constexpr const char *kOnCount = "%d of %d on";
constexpr const char *kLayouts = "Layouts";
constexpr const char *kLayoutsTitle = "Layouts";
constexpr const char *kLayoutsHint = "Each of these screens comes in three layouts. Mix them like themes.";
constexpr const char *kDone = "Done";
constexpr const char *kTilesHint = "Tap to turn on or off \xC2\xB7 hold for options";
constexpr const char *kOn = "ON";
constexpr const char *kOff = "OFF";
constexpr const char *kLayoutRow = "Layout";
constexpr const char *kOptionRow = "When";
constexpr const char *kPresetPicks = "Your picks";
constexpr const char *kPresetA = "All A";
constexpr const char *kPresetB = "All B";
constexpr const char *kPresetC = "All C";
constexpr const char *kLetters[] = {"A", "B", "C"};

// Screens with three layouts, in the order of Layout in settings.h.
struct LayoutInfo {
  const char *name;
  const char *variants[3];
};
const LayoutInfo kLayoutInfo[kLayoutCount] = {
    {"Quick add task", {"Keyboard", "Frequent tiles", "Sheet over list"}},
    {"Focus timer", {"Big ring", "Split", "Header strip"}},
    {"Daily summary", {"Stat tiles", "Timeline", "Briefing"}},
    {"Conflict alerts", {"Banner on Home", "Compare popup", "Marked in Week"}},
    {"Leave-by alert", {"Full screen", "Card on Home", "Header countdown"}},
    {"Countdowns and dates", {"Home column", "Ribbon", "Dates tab"}},
    {"End-of-day review", {"Checklist", "One at a time", "Plan by day"}},
    {"Features screen", {"List", "Tabs", "Tiles"}},
};

enum class Opt { None, SummaryAt, ReviewAt, LeaveLead, FocusMinutes };
enum class Tab { Alerts, Home, Tasks, Sensors, Device, Count };
const char *kTabNames[] = {"Alerts", "Home screen", "Tasks", "Sensors", "Device"};

struct Feature {
  const char *name;
  const char *sub;   // may contain %s for the option value
  bool Settings::*on;
  Opt opt;
  int8_t layout;     // index into kLayoutInfo, -1 if none
  Tab tab;
};
const Feature kFeatures[] = {
    {"Daily summary", "Every morning at %s", &Settings::summary, Opt::SummaryAt, (int8_t)Layout::Summary, Tab::Alerts},
    {"End-of-day review", "Every evening at %s", &Settings::review, Opt::ReviewAt, (int8_t)Layout::Review, Tab::Alerts},
    {"Conflict alerts", "Overlapping meetings, all accounts", &Settings::conflicts, Opt::None, (int8_t)Layout::Conflicts, Tab::Alerts},
    {"Leave-by alerts", "%s before you must leave", &Settings::leaveBy, Opt::LeaveLead, (int8_t)Layout::LeaveBy, Tab::Alerts},
    {"Reminder sound", "Chime on the speaker", &Settings::sound, Opt::None, -1, Tab::Alerts},
    {"Focus timer", "%s, hold a task to start", &Settings::focus, Opt::FocusMinutes, (int8_t)Layout::Focus, Tab::Tasks},
    {"Quick add task", "Add tasks from the display", &Settings::quickAdd, Opt::None, (int8_t)Layout::QuickAdd, Tab::Tasks},
    {"Countdowns", "Starred events on Home", &Settings::countdowns, Opt::None, (int8_t)Layout::Dates, Tab::Home},
    {"Birthdays and holidays", "From Contacts and Google", &Settings::dates, Opt::None, (int8_t)Layout::Dates, Tab::Home},
    {"Ambient light", "Dims in a dark room", &Settings::ambient, Opt::None, -1, Tab::Sensors},
    {"Motion wake", "PIR sensor on the UART header RX pin", &Settings::motion, Opt::None, -1, Tab::Sensors},
    {"Offline copy", "Keep the last sync on the TF card", &Settings::offlineCopy, Opt::None, -1, Tab::Device},
};
constexpr int kFeatureCount = sizeof(kFeatures) / sizeof(kFeatures[0]);
constexpr const char *kNeedsLightSensor = "Needs a BH1750 light sensor on I2C";

// Values the option chip steps through.
const uint16_t kSummaryTimes[] = {6 * 60, 6 * 60 + 30, 7 * 60, 7 * 60 + 30, 8 * 60, 8 * 60 + 30, 9 * 60};
const uint16_t kReviewTimes[] = {17 * 60, 18 * 60, 19 * 60, 20 * 60, 20 * 60 + 30, 21 * 60, 22 * 60};
const uint8_t kLeadMinutes[] = {5, 10, 15, 20, 30};
const uint8_t kFocusMinutes[] = {15, 20, 25, 30, 45, 50, 60};
constexpr const char *kMinutes = "%d min";

constexpr int BODY_Y = HEADER_H + GAP;
constexpr int BODY_H = SCREEN_H - BODY_Y - PAD;
constexpr int TAB_W = 200;

// ---- State -------------------------------------------------------------------

lv_obj_t *countLabel, *body;
Tab tab = Tab::Alerts;
lv_obj_t *dialog = nullptr;
int dialogFeature = -1;
lv_obj_t *picker = nullptr;

// ---- Code --------------------------------------------------------------------

String fmtMinutes(uint16_t m) {
  return strf("%02d:%02d", m / 60, m % 60);
}

String optionValue(Opt o) {
  switch (o) {
    case Opt::SummaryAt: return fmtMinutes(g_settings.summaryAt);
    case Opt::ReviewAt: return fmtMinutes(g_settings.reviewAt);
    case Opt::LeaveLead: return strf(kMinutes, g_settings.leaveLead);
    case Opt::FocusMinutes: return strf(kMinutes, g_settings.focusMinutes);
    default: return "";
  }
}

template <typename T, size_t N>
T nextOf(const T (&values)[N], T current) {
  for (size_t i = 0; i < N; i++) {
    if (values[i] > current) return values[i];
  }
  return values[0];
}

void stepOption(Opt o) {
  Settings s = g_settings;
  switch (o) {
    case Opt::SummaryAt: s.summaryAt = nextOf(kSummaryTimes, s.summaryAt); break;
    case Opt::ReviewAt: s.reviewAt = nextOf(kReviewTimes, s.reviewAt); break;
    case Opt::LeaveLead: s.leaveLead = nextOf(kLeadMinutes, s.leaveLead); break;
    case Opt::FocusMinutes: s.focusMinutes = nextOf(kFocusMinutes, s.focusMinutes); break;
    default: return;
  }
  ui_save_settings(s);
}

bool available(int i) {
  return kFeatures[i].on != &Settings::ambient || light_present();
}

bool isOn(int i) {
  return g_settings.*(kFeatures[i].on) && available(i);
}

String subText(int i) {
  const Feature &f = kFeatures[i];
  if (!available(i)) return kNeedsLightSensor;
  return f.opt == Opt::None ? String(f.sub) : strf(f.sub, optionValue(f.opt).c_str());
}

// Changing a feature can rebuild every screen, which deletes the widget that
// was tapped, so all changes run after the event.
void setFeature(int i, bool on) {
  lv_async_call([](void *p) {
    const intptr_t v = (intptr_t)p;
    Settings s = g_settings;
    s.*(kFeatures[v >> 1].on) = v & 1;
    ui_save_settings(s);
    features_refresh();
  }, (void *)(intptr_t)((i << 1) | (on ? 1 : 0)));
}

void showDialog(int i);
void showPicker();

void stepOptionLater(int i) {
  lv_async_call([](void *p) {
    stepOption(kFeatures[(intptr_t)p].opt);
    features_refresh();
    if (dialog) showDialog((intptr_t)p);
  }, (void *)(intptr_t)i);
}

lv_obj_t *featureSwitch(lv_obj_t *parent, int i) {
  lv_obj_t *sw = mk_switch(parent, isOn(i), [](lv_event_t *e) {
    setFeature((intptr_t)lv_event_get_user_data(e), lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
  }, (void *)(intptr_t)i);
  if (!available(i)) lv_obj_add_state(sw, LV_STATE_DISABLED);
  return sw;
}

lv_obj_t *textColumn(lv_obj_t *parent, int i, int w) {
  const Theme &t = theme();
  lv_obj_t *col = mk_box(parent, 0, 0);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_width(col, w);
  set_flex(col, LV_FLEX_FLOW_COLUMN, 2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
  mk_label(col, &font_14b, t.text, kFeatures[i].name);
  lv_obj_t *sub = mk_label(col, &font_12, available(i) ? t.muted : t.warnFg, subText(i).c_str());
  lv_label_set_long_mode(sub, LV_LABEL_LONG_DOT);
  lv_obj_set_width(sub, w);
  return col;
}

// A: two columns of rows.
void buildList() {
  const Theme &t = theme();
  lv_obj_t *grid = mk_scroll_column(body, 0);
  lv_obj_set_size(grid, SCREEN_W - 2 * PAD, BODY_H);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_column(grid, 16, 0);
  const int w = (SCREEN_W - 2 * PAD - 16) / 2;
  for (int i = 0; i < kFeatureCount; i++) {
    lv_obj_t *row = mk_button(grid, t.bg, 0);
    lv_obj_set_size(row, w, 58);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, c(t.panel), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    set_flex(row, LV_FLEX_FLOW_ROW, 10, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER);
    textColumn(row, i, w - 76);
    featureSwitch(row, i);
    on_click(row, [](lv_event_t *e) { showDialog((intptr_t)lv_event_get_user_data(e)); }, (void *)(intptr_t)i);
  }
}

// B: tabs on the left, the tab's features with their options on the right.
void buildTabs() {
  const Theme &t = theme();
  lv_obj_t *nav = mk_box(body, 0, 0);
  lv_obj_set_style_bg_opa(nav, LV_OPA_TRANSP, 0);
  lv_obj_set_size(nav, TAB_W, BODY_H);
  set_flex(nav, LV_FLEX_FLOW_COLUMN, 4, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  for (int k = 0; k < (int)Tab::Count; k++) {
    const bool on = (Tab)k == tab;
    lv_obj_t *b = mk_button(nav, on ? t.raised : t.bg, 12);
    lv_obj_set_size(b, TAB_W, TOUCH + 2);
    lv_obj_set_style_pad_hor(b, 14, 0);
    set_flex(b, LV_FLEX_FLOW_ROW, 8);
    mk_label(b, &font_14b, on ? t.text : t.muted, kTabNames[k]);
    on_click(b, [](lv_event_t *e) {
      tab = (Tab)(intptr_t)lv_event_get_user_data(e);
      lv_async_call([](void *) { features_refresh(); }, nullptr);
    }, (void *)(intptr_t)k);
  }

  const int w = SCREEN_W - 2 * PAD - TAB_W - GAP;
  lv_obj_t *panel = mk_scroll_column(body, 6);
  lv_obj_set_size(panel, w, BODY_H);
  lv_obj_set_pos(panel, TAB_W + GAP, 0);
  lv_obj_add_flag(panel, LV_OBJ_FLAG_IGNORE_LAYOUT);
  for (int i = 0; i < kFeatureCount; i++) {
    if (kFeatures[i].tab != tab) continue;
    lv_obj_t *row = mk_box(panel, t.card, 14);
    lv_obj_set_size(row, w, 64);
    lv_obj_set_style_pad_left(row, 16, 0);
    lv_obj_set_style_pad_right(row, 10, 0);
    set_flex(row, LV_FLEX_FLOW_ROW, 10);
    lv_obj_t *col = textColumn(row, i, w - 26 - 66 - (kFeatures[i].opt != Opt::None ? 104 : 0));
    lv_obj_set_flex_grow(col, 1);
    if (kFeatures[i].opt != Opt::None) {
      lv_obj_t *chip = mk_chip(row, optionValue(kFeatures[i].opt).c_str(), false);
      lv_obj_set_style_border_color(chip, c(t.line), 0);
      lv_obj_set_style_border_width(chip, 1, 0);
      on_click(chip, [](lv_event_t *e) { stepOptionLater((intptr_t)lv_event_get_user_data(e)); }, (void *)(intptr_t)i);
    }
    featureSwitch(row, i);
  }
}

// C: tiles; tap toggles, hold opens the options.
void buildTiles() {
  const Theme &t = theme();
  lv_obj_t *grid = mk_box(body, 0, 0);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_size(grid, SCREEN_W - 2 * PAD, BODY_H);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_row(grid, 8, 0);
  lv_obj_set_style_pad_column(grid, 8, 0);
  const int w = (SCREEN_W - 2 * PAD - 4 * 8) / 5;
  const int h = (BODY_H - 2 * 8) / 3;
  for (int i = 0; i < kFeatureCount; i++) {
    const bool on = isOn(i);
    lv_obj_t *tile = mk_button(grid, on ? t.warnBg : t.panel, 14);
    lv_obj_set_size(tile, w, h);
    lv_obj_set_style_border_width(tile, 1, 0);
    lv_obj_set_style_border_color(tile, c(on ? t.accent : t.line), 0);
    lv_obj_set_style_pad_all(tile, 10, 0);
    set_flex(tile, LV_FLEX_FLOW_COLUMN, 4, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START);
    lv_obj_t *state = mk_label(tile, &font_12, on ? t.accent : t.muted, on ? kOn : kOff);
    lv_obj_set_style_text_letter_space(state, 1, 0);
    lv_obj_t *name = mk_label(tile, &font_14b, t.text, kFeatures[i].name);
    lv_obj_set_width(name, w - 20);
    lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);
    const String sub = kFeatures[i].opt != Opt::None ? optionValue(kFeatures[i].opt) : subText(i);
    lv_obj_t *sl = mk_label(tile, &font_12, t.muted, sub.c_str());
    lv_obj_set_width(sl, w - 20);
    lv_label_set_long_mode(sl, LV_LABEL_LONG_DOT);
    lv_obj_add_event_cb(tile, [](lv_event_t *e) {
      const int i = (intptr_t)lv_event_get_user_data(e);
      const lv_event_code_t code = lv_event_get_code(e);
      if (code != LV_EVENT_LONG_PRESSED && code != LV_EVENT_CLICKED) return;
      if (code == LV_EVENT_LONG_PRESSED) {
        lv_obj_add_flag(lv_event_get_target(e), LV_OBJ_FLAG_USER_1);  // swallow the click that follows
        showDialog(i);
      } else if (lv_obj_has_flag(lv_event_get_target(e), LV_OBJ_FLAG_USER_1)) {
        lv_obj_clear_flag(lv_event_get_target(e), LV_OBJ_FLAG_USER_1);
      } else if (available(i)) {
        setFeature(i, !isOn(i));
      }
    }, LV_EVENT_ALL, (void *)(intptr_t)i);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
  }
  lv_obj_t *more = mk_button(grid, t.card, 14);
  lv_obj_set_size(more, 2 * w + 8, h);
  lv_obj_set_style_pad_all(more, 14, 0);
  set_flex(more, LV_FLEX_FLOW_COLUMN, 4, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
  lv_obj_t *top = mk_box(more, 0, 0);
  lv_obj_set_style_bg_opa(top, LV_OPA_TRANSP, 0);
  set_flex(top, LV_FLEX_FLOW_ROW, 8);
  mk_label(top, &font_16, t.accent, ICON_LAYERS);
  mk_label(top, &font_14b, t.text, kLayouts);
  mk_label(more, &font_12, t.muted, kTilesHint);
  on_click(more, [](lv_event_t *) { showPicker(); });
}

// Options for one feature: on/off, its setting and its layout.
void showDialog(int i) {
  close_overlay(&dialog);
  dialogFeature = i;
  const Theme &t = theme();
  const Feature &f = kFeatures[i];
  mk_overlay(false, &dialog);
  lv_obj_t *card = mk_box(dialog, t.card, 24);
  lv_obj_set_width(card, 560);
  lv_obj_set_style_pad_hor(card, 28, 0);
  lv_obj_set_style_pad_ver(card, 24, 0);
  set_flex(card, LV_FLEX_FLOW_COLUMN, 14, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_center(card);

  lv_obj_t *head = mk_box(card, 0, 0);
  lv_obj_set_style_bg_opa(head, LV_OPA_TRANSP, 0);
  lv_obj_set_width(head, 504);
  set_flex(head, LV_FLEX_FLOW_ROW, 0, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *col = textColumn(head, i, 380);
  lv_obj_set_style_text_font(lv_obj_get_child(col, 0), &font_20b, 0);
  featureSwitch(head, i);

  if (f.opt != Opt::None) {
    lv_obj_t *row = mk_box(card, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_width(row, 504);
    set_flex(row, LV_FLEX_FLOW_ROW, 10);
    lv_obj_t *l = mk_label(row, &font_14b, t.muted, kOptionRow);
    lv_obj_set_width(l, 70);
    on_click(mk_chip(row, optionValue(f.opt).c_str(), true), [](lv_event_t *) { stepOptionLater(dialogFeature); });
  }
  if (f.layout >= 0) {
    lv_obj_t *row = mk_box(card, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_width(row, 504);
    set_flex(row, LV_FLEX_FLOW_ROW, 8);
    lv_obj_t *l = mk_label(row, &font_14b, t.muted, kLayoutRow);
    lv_obj_set_width(l, 70);
    for (int v = 0; v < 3; v++) {
      lv_obj_t *chip = mk_chip(row, kLayoutInfo[f.layout].variants[v], g_settings.layouts[f.layout] == v);
      on_click(chip, [](lv_event_t *e) {
        lv_async_call([](void *p) {
          Settings s = g_settings;
          s.layouts[kFeatures[dialogFeature].layout] = (uint8_t)(intptr_t)p;
          ui_save_settings(s);
          showDialog(dialogFeature);
        }, lv_event_get_user_data(e));
      }, (void *)(intptr_t)v);
    }
  }
  lv_obj_t *done = mk_text_button(card, kDone, true, 504);
  on_click(done, [](lv_event_t *) {
    close_overlay(&dialog);
    features_refresh();
  });
}

void applyPreset(const uint8_t *values) {
  Settings s = g_settings;
  memcpy(s.layouts, values, sizeof(s.layouts));
  ui_save_settings(s);
}

void showPicker() {
  close_overlay(&picker);
  const Theme &t = theme();
  mk_overlay(false, &picker);
  lv_obj_t *card = mk_box(picker, t.card, 24);
  lv_obj_set_size(card, 760, SCREEN_H - 24);
  lv_obj_set_style_pad_hor(card, 22, 0);
  lv_obj_set_style_pad_ver(card, 16, 0);
  set_flex(card, LV_FLEX_FLOW_COLUMN, 10, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_center(card);

  lv_obj_t *head = mk_box(card, 0, 0);
  lv_obj_set_style_bg_opa(head, LV_OPA_TRANSP, 0);
  lv_obj_set_width(head, 716);
  set_flex(head, LV_FLEX_FLOW_ROW, 8);
  lv_obj_t *title = mk_label(head, &font_20b, t.text, kLayoutsTitle);
  lv_obj_set_flex_grow(title, 1);
  // Presets, like a set of themes for the layouts.
  static const uint8_t kAllA[kLayoutCount] = {0, 0, 0, 0, 0, 0, 0, 0};
  static const uint8_t kAllB[kLayoutCount] = {1, 1, 1, 1, 1, 1, 1, 1};
  static const uint8_t kAllC[kLayoutCount] = {2, 2, 2, 2, 2, 2, 2, 2};
  struct Preset {
    const char *name;
    const uint8_t *values;
  };
  static const Preset kPresets[] = {{kPresetPicks, kDefaultLayouts}, {kPresetA, kAllA}, {kPresetB, kAllB}, {kPresetC, kAllC}};
  for (const Preset &p : kPresets) {
    const bool on = memcmp(g_settings.layouts, p.values, kLayoutCount) == 0;
    on_click(mk_chip(head, p.name, on), [](lv_event_t *e) {
      lv_async_call([](void *p) {
        applyPreset(static_cast<const uint8_t *>(p));
        showPicker();
      }, lv_event_get_user_data(e));
    }, (void *)p.values);
  }
  on_click(mk_text_button(head, kDone, false, 90), [](lv_event_t *) {
    close_overlay(&picker);
    features_refresh();
  });
  mk_label(card, &font_12, t.muted, kLayoutsHint);

  lv_obj_t *list = mk_scroll_column(card, 0);
  lv_obj_set_width(list, 716);
  lv_obj_set_flex_grow(list, 1);
  for (int k = 0; k < kLayoutCount; k++) {
    lv_obj_t *row = mk_box(list, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_size(row, 708, 42);
    set_flex(row, LV_FLEX_FLOW_ROW, 6);
    lv_obj_t *name = mk_label(row, &font_14b, t.text, kLayoutInfo[k].name);
    lv_obj_set_width(name, 196);
    for (int v = 0; v < 3; v++) {
      const String label = String(kLetters[v]) + "  " + kLayoutInfo[k].variants[v];
      lv_obj_t *chip = mk_chip(row, label.c_str(), g_settings.layouts[k] == v, 38);
      lv_obj_set_width(chip, 166);
      on_click(chip, [](lv_event_t *e) {
        lv_async_call([](void *p) {
          const intptr_t v = (intptr_t)p;
          Settings s = g_settings;
          s.layouts[v / 3] = v % 3;
          ui_save_settings(s);
          showPicker();
        }, lv_event_get_user_data(e));
      }, (void *)(intptr_t)(k * 3 + v));
    }
  }
}

}  // namespace

lv_obj_t *features_create() {
  const Theme &t = theme();
  lv_obj_t *scr = mk_screen();
  lv_obj_t *h = mk_back_header(scr, kTitle, Screen::Settings);
  countLabel = mk_label(h, &font_14b, t.accent, "");
  lv_obj_t *lay = mk_button(h, t.card, TOUCH / 2);
  lv_obj_set_size(lay, LV_SIZE_CONTENT, TOUCH);
  lv_obj_set_style_pad_hor(lay, 16, 0);
  set_flex(lay, LV_FLEX_FLOW_ROW, 8);
  mk_label(lay, &font_14, t.text, ICON_LAYERS);
  mk_label(lay, &font_14b, t.text, kLayouts);
  on_click(lay, [](lv_event_t *) { showPicker(); });

  body = mk_box(scr, 0, 0);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_pos(body, PAD, BODY_Y);
  lv_obj_set_size(body, SCREEN_W - 2 * PAD, BODY_H);
  return scr;
}

void features_refresh() {
  int on = 0;
  for (int i = 0; i < kFeatureCount; i++) on += isOn(i);
  lv_label_set_text(countLabel, strf(kOnCount, on, kFeatureCount).c_str());
  lv_obj_clean(body);
  switch (g_settings.layout(Layout::Features)) {
    case 0: buildList(); break;
    case 1: buildTabs(); break;
    default: buildTiles(); break;
  }
}

void layout_picker_show() {
  showPicker();
}

int features_on_count() {
  int on = 0;
  for (int i = 0; i < kFeatureCount; i++) on += isOn(i);
  return on;
}
