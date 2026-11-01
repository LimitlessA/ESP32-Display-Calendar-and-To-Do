// Settings > Features (round 7, design C): a grid of tiles, one per feature. A tap turns
// the feature on or off; holding a tile opens its options, including its layout A, B or C.
#include "presence.h"
#include "sensors.h"
#include "ui_internal.h"

namespace {

// ---- Texts and settings ------------------------------------------------------

constexpr const char *kTitle = "Features";
constexpr const char *kOnCount = "%d of %d on";
constexpr const char *kHint = "Tap to turn on/off \xC2\xB7 hold for options";
constexpr const char *kDone = "Done";
constexpr const char *kLayoutRow = "Layout";
constexpr const char *kOptionRow = "When";
constexpr const char *kPhoneRow = "Phone";
constexpr const char *kDeskRow = "Desk";
constexpr const char *kSitRow = "Sit";
constexpr const char *kStandRow = "Stand";
constexpr const char *kDeskLogin = "Desk address, login and connection test";
constexpr const char *kPair = "Pair a phone";
constexpr const char *kForget = "Forget";
constexpr const char *kForgotten = "Paired phones removed. Also remove \"Calendar display\" in your phone's Bluetooth settings.";
constexpr const char *kVariant = "%s \xC2\xB7 %s";  // letter, layout name
constexpr const char *kLetters[] = {"A", "B", "C"};

// The names of each screen's three layouts, in the order of Layout in settings.h.
const char *const kVariants[kLayoutCount][3] = {
    {"Keyboard", "Frequent tiles", "Sheet over list"},        // quick add task
    {"Big ring", "Split", "Header strip"},                    // focus timer
    {"Stat tiles", "Timeline", "Briefing"},                   // daily summary
    {"Banner on Home", "Compare popup", "Marked in Week"},    // conflict alerts
    {"Full screen", "Card on Home", "Header countdown"},      // leave-by alert
    {"Home column", "Ribbon", "Dates tab"},                   // countdowns and dates
    {"Checklist", "One at a time", "Plan by day"},            // end-of-day review
    {"Tiles", "Tiles", "Tiles"},                              // Features screen: always tiles now
    {"Card progress", "Full screen", "Glowing edge"},         // meeting timer
    {"Welcome back", "Privacy mode", "Away screen"},          // phone presence
    {"On the card", "Big counter", "Weekly receipt"},         // meeting cost
    {"Glowing tasks", "Nudge popup", "Radar view"},           // procrastination radar
    {"Wheel", "Card deck", "Slot machine"},                   // task roulette
    {"Dock on Home", "Desk screen", "Follows calendar"},      // smart desk
};

enum class Opt { None, SummaryAt, ReviewAt, LeaveLead, FocusMinutes, CostRate, AwayMinutes, RadarMoves };

struct Feature {
  const char *name;
  const char *sub;   // may contain %s for the option value
  bool Settings::*on;
  Opt opt;
  int8_t layout;     // index into kVariants, -1 if none
};
const Feature kFeatures[] = {
    {"Daily summary", "Every morning at %s", &Settings::summary, Opt::SummaryAt, (int8_t)Layout::Summary},
    {"End-of-day review", "Every evening at %s", &Settings::review, Opt::ReviewAt, (int8_t)Layout::Review},
    {"Conflict alerts", "Overlapping meetings, all accounts", &Settings::conflicts, Opt::None, (int8_t)Layout::Conflicts},
    {"Leave-by alerts", "%s before you must leave", &Settings::leaveBy, Opt::LeaveLead, (int8_t)Layout::LeaveBy},
    {"Meeting timer", "Time left, chime 5 min before the end", &Settings::meetTimer, Opt::None, (int8_t)Layout::MeetTimer},
    {"Meeting cost", "People x %s per hour", &Settings::costMeter, Opt::CostRate, (int8_t)Layout::Cost},
    {"Reminder sound", "Chime on the speaker", &Settings::sound, Opt::None, -1},
    {"Focus timer", "%s, hold a task to start", &Settings::focus, Opt::FocusMinutes, (int8_t)Layout::Focus},
    {"Quick add task", "Add tasks from the display", &Settings::quickAdd, Opt::None, (int8_t)Layout::QuickAdd},
    {"Procrastination radar", "Stuck after %s moves", &Settings::radar, Opt::RadarMoves, (int8_t)Layout::Radar},
    {"Task roulette", "Let the display pick a task", &Settings::roulette, Opt::None, (int8_t)Layout::Roulette},
    {"Countdowns", "Starred events on Home", &Settings::countdowns, Opt::None, (int8_t)Layout::Dates},
    {"Birthdays and holidays", "From Contacts and Google", &Settings::dates, Opt::None, (int8_t)Layout::Dates},
    {"Ambient light", "Dims in a dark room", &Settings::ambient, Opt::None, -1},
    {"Motion wake", "PIR sensor on the UART header RX pin", &Settings::motion, Opt::None, -1},
    {"Phone presence", "Away after %s without your phone", &Settings::presence, Opt::AwayMinutes, (int8_t)Layout::Presence},
    {"Smart desk", "Standing desk on Wi-Fi", &Settings::desk, Opt::None, (int8_t)Layout::Desk},
    {"Offline copy", "Keep the last sync on the TF card", &Settings::offlineCopy, Opt::None, -1},
};
constexpr int kFeatureCount = sizeof(kFeatures) / sizeof(kFeatures[0]);
constexpr const char *kNeedsLightSensor = "Needs a BH1750 light sensor on I2C";

constexpr const char *kMinutes = "%d min";
constexpr const char *kMoney = "\u20AC%d";

constexpr int BODY_Y = HEADER_H + GAP;
constexpr int BODY_H = SCREEN_H - BODY_Y - PAD;
constexpr int HINT_H = 16;
constexpr int COLS = 5;
constexpr int TILE_GAP = 8;
constexpr int TILE_W = (SCREEN_W - 2 * PAD - (COLS - 1) * TILE_GAP) / COLS;
constexpr int ROWS = (kFeatureCount + COLS - 1) / COLS;
constexpr int GRID_H = BODY_H - HINT_H - TILE_GAP;
constexpr int TILE_H = (GRID_H - (ROWS - 1) * TILE_GAP) / ROWS;  // every tile on one screen, no scrolling
constexpr int DIALOG_W = 640;
constexpr int INNER_W = DIALOG_W - 56;

// ---- State -------------------------------------------------------------------

lv_obj_t *countLabel, *body;
lv_obj_t *dialog = nullptr;
int dialogFeature = -1;

// ---- Code --------------------------------------------------------------------

String fmtMinutes(uint16_t m) {
  return strf("%02d:%02d", m / 60, m % 60);
}

const char *optionLabel(Opt o) {
  switch (o) {
    case Opt::LeaveLead: return "Warn";
    case Opt::FocusMinutes: return "Length";
    case Opt::CostRate: return "Rate";
    case Opt::AwayMinutes: return "Away";
    case Opt::RadarMoves: return "Stuck";
    default: return kOptionRow;
  }
}

String fmtMin(int v) { return strf(kMinutes, v); }
String fmtMoney(int v) { return strf(kMoney, v); }
String fmtCount(int v) { return String(v); }
String fmtClock(int v) { return fmtMinutes((uint16_t)v); }

// What a -/+ stepper does for each option: range, step and how the value reads.
struct OptRange {
  int min, max, step;
  StepFmt fmt;
};

OptRange optRange(Opt o) {
  switch (o) {
    case Opt::SummaryAt: return {5 * 60, 11 * 60, 30, fmtClock};
    case Opt::ReviewAt: return {16 * 60, 23 * 60, 30, fmtClock};
    case Opt::LeaveLead: return {5, 60, 5, fmtMin};
    case Opt::FocusMinutes: return {5, 90, 5, fmtMin};
    case Opt::CostRate: return {5, 500, 5, fmtMoney};
    case Opt::AwayMinutes: return {5, 60, 5, fmtMin};
    case Opt::RadarMoves: return {2, 10, 1, fmtCount};
    default: return {0, 0, 1, fmtCount};
  }
}

int optionNumber(Opt o, const Settings &s) {
  switch (o) {
    case Opt::SummaryAt: return s.summaryAt;
    case Opt::ReviewAt: return s.reviewAt;
    case Opt::LeaveLead: return s.leaveLead;
    case Opt::FocusMinutes: return s.focusMinutes;
    case Opt::CostRate: return s.costRate;
    case Opt::AwayMinutes: return s.awayMinutes;
    case Opt::RadarMoves: return s.radarMoves;
    default: return 0;
  }
}

void applyOption(Settings &s, Opt o, int v) {
  switch (o) {
    case Opt::SummaryAt: s.summaryAt = v; break;
    case Opt::ReviewAt: s.reviewAt = v; break;
    case Opt::LeaveLead: s.leaveLead = v; break;
    case Opt::FocusMinutes: s.focusMinutes = v; break;
    case Opt::CostRate: s.costRate = v; break;
    case Opt::AwayMinutes: s.awayMinutes = v; break;
    case Opt::RadarMoves: s.radarMoves = v; break;
    default: break;
  }
}

// Steps are collected and saved a moment after the last one, so holding + or - does not
// rebuild every screen on each step.
Settings pending;
bool havePending = false;
lv_timer_t *saveTimer = nullptr;
lv_obj_t *dialogSub = nullptr;  // the dialog's subtitle, kept up to date while stepping

const Settings &cur() { return havePending ? pending : g_settings; }

String optionValue(Opt o) {
  const OptRange r = optRange(o);
  return r.fmt ? r.fmt(optionNumber(o, cur())) : String("");
}

bool available(int i);
String subText(int i);

void flushPending() {
  if (saveTimer) {
    lv_timer_del(saveTimer);
    saveTimer = nullptr;
  }
  if (!havePending) return;
  havePending = false;
  ui_save_settings(pending);
  features_refresh();
}

void stepped(int value, void *user) {
  const int feature = (int)(intptr_t)user;
  if (!havePending) {
    pending = g_settings;
    havePending = true;
  }
  applyOption(pending, kFeatures[feature].opt, value);
  if (dialogSub) lv_label_set_text(dialogSub, subText(feature).c_str());
  if (saveTimer) {
    lv_timer_reset(saveTimer);
  } else {
    saveTimer = lv_timer_create([](lv_timer_t *) {
      saveTimer = nullptr;  // one shot: LVGL deletes it after this run
      ui_save_settings(pending);
      havePending = false;
      features_refresh();
    }, 600, nullptr);
    lv_timer_set_repeat_count(saveTimer, 1);
  }
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
    flushPending();
    Settings s = g_settings;
    s.*(kFeatures[v >> 1].on) = v & 1;
    ui_save_settings(s);
    features_refresh();
  }, (void *)(intptr_t)((i << 1) | (on ? 1 : 0)));
}

void showDialog(int i);

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

// The grid: a tap toggles, holding opens the options (and the layout).
void buildGrid() {
  const Theme &t = theme();
  lv_obj_t *hint = mk_label(body, &font_12, t.muted, kHint);
  lv_obj_set_height(hint, HINT_H);

  lv_obj_t *grid = mk_box(body, 0, 0);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_size(grid, SCREEN_W - 2 * PAD, GRID_H);
  set_flex(grid, LV_FLEX_FLOW_ROW_WRAP, TILE_GAP, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(grid, TILE_GAP, 0);
  for (int i = 0; i < kFeatureCount; i++) {
    const bool on = isOn(i);
    lv_obj_t *tile = mk_button(grid, t.card, 14);
    lv_obj_set_size(tile, TILE_W, TILE_H);
    lv_obj_set_style_pad_hor(tile, 12, 0);
    lv_obj_t *name = mk_label(tile, &font_14b, on ? t.text : t.muted, kFeatures[i].name);
    lv_obj_set_width(name, TILE_W - 24);  // two lines at most
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_align(mk_dot(tile, on ? t.good : t.line, 10), LV_ALIGN_TOP_RIGHT, 2, 0);
    // LVGL sends SHORT_CLICKED only when the press was not a long press.
    lv_obj_add_event_cb(tile, [](lv_event_t *e) {
      const int i = (intptr_t)lv_event_get_user_data(e);
      if (available(i)) setFeature(i, !isOn(i));
    }, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)i);
    lv_obj_add_event_cb(tile, [](lv_event_t *e) { showDialog((intptr_t)lv_event_get_user_data(e)); },
                        LV_EVENT_LONG_PRESSED, (void *)(intptr_t)i);
  }
}

// Options for one feature: on/off, its setting and its layout.
void showDialog(int i) {
  close_overlay(&dialog);
  dialogSub = nullptr;
  dialogFeature = i;
  const Theme &t = theme();
  const Feature &f = kFeatures[i];
  mk_overlay(false, &dialog);
  lv_obj_t *card = mk_box(dialog, t.card, 24);
  lv_obj_set_width(card, DIALOG_W);
  lv_obj_set_style_pad_hor(card, 28, 0);
  lv_obj_set_style_pad_ver(card, 24, 0);
  set_flex(card, LV_FLEX_FLOW_COLUMN, 14, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_center(card);

  lv_obj_t *head = mk_box(card, 0, 0);
  lv_obj_set_style_bg_opa(head, LV_OPA_TRANSP, 0);
  lv_obj_set_width(head, INNER_W);
  set_flex(head, LV_FLEX_FLOW_ROW, 0, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *col = textColumn(head, i, INNER_W - 80);
  lv_obj_set_style_text_font(lv_obj_get_child(col, 0), &font_20b, 0);
  dialogSub = lv_obj_get_child(col, 1);
  featureSwitch(head, i);

  if (f.opt != Opt::None) {
    lv_obj_t *row = mk_box(card, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_width(row, INNER_W);
    set_flex(row, LV_FLEX_FLOW_ROW, 10);
    lv_obj_t *l = mk_label(row, &font_14b, t.muted, optionLabel(f.opt));
    lv_obj_set_width(l, 70);
    const OptRange r = optRange(f.opt);
    mk_stepper(row, optionNumber(f.opt, cur()), r.min, r.max, r.step, r.fmt, stepped, (void *)(intptr_t)i, 96);
  }
  if (f.on == &Settings::desk) {
    lv_obj_t *row = mk_box(card, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_width(row, INNER_W);
    set_flex(row, LV_FLEX_FLOW_ROW, 8);
    lv_obj_t *l = mk_label(row, &font_14b, t.muted, kDeskRow);
    lv_obj_set_width(l, 70);
    const String st = g_settings.desk ? desk_status_text() : String(g_settings.deskHost);
    lv_obj_t *status = mk_label(row, &font_14, t.text, st.c_str());
    lv_obj_set_width(status, INNER_W - 80);
    lv_label_set_long_mode(status, LV_LABEL_LONG_DOT);
    // Which memory buttons mean sitting and standing: each one is tapped directly.
    static const char *const kSlotNames[] = {"M1", "M2", "M3", "M4"};
    for (int stand = 0; stand < 2; stand++) {
      lv_obj_t *slots = mk_box(card, 0, 0);
      lv_obj_set_style_bg_opa(slots, LV_OPA_TRANSP, 0);
      lv_obj_set_width(slots, INNER_W);
      set_flex(slots, LV_FLEX_FLOW_ROW, 10);
      lv_obj_t *sl = mk_label(slots, &font_14b, t.muted, stand ? kStandRow : kSitRow);
      lv_obj_set_width(sl, 70);
      const uint8_t v = stand ? g_settings.deskStandSlot : g_settings.deskSitSlot;
      mk_segmented(slots, kSlotNames, 4, v - 1, 64, [](int idx, void *user) {
        lv_async_call([](void *p) {
          const intptr_t v = (intptr_t)p;  // slot * 2 + (stand ? 1 : 0)
          flushPending();
          Settings s = g_settings;
          (v & 1 ? s.deskStandSlot : s.deskSitSlot) = (uint8_t)(v >> 1);
          ui_save_settings(s);
          if (dialog) showDialog(dialogFeature);
        }, (void *)(intptr_t)(((idx + 1) << 1) | (user ? 1 : 0)));
      }, (void *)(intptr_t)stand);
    }
    lv_obj_t *login = mk_text_button(card, kDeskLogin, false, INNER_W);
    on_click(login, [](lv_event_t *) {
      lv_async_call([](void *) {
        flushPending();
        desk_settings_show();
      }, nullptr);
    });
  }
  if (f.on == &Settings::presence) {
    lv_obj_t *row = mk_box(card, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_width(row, INNER_W);
    set_flex(row, LV_FLEX_FLOW_ROW, 8);
    lv_obj_t *l = mk_label(row, &font_14b, t.muted, kPhoneRow);
    lv_obj_set_width(l, 70);
    lv_obj_t *status = mk_label(row, &font_14, t.text, presence_status().c_str());
    lv_obj_set_flex_grow(status, 1);
    on_click(mk_chip(row, kPair, false), [](lv_event_t *) {
      lv_async_call([](void *) {
        close_overlay(&dialog);
        presence_pair_show();
      }, nullptr);
    });
    if (presence_paired_count() > 0) {
      on_click(mk_chip(row, kForget, false), [](lv_event_t *) {
        lv_async_call([](void *) {
          presence_forget();
          toast(kForgotten);
          showDialog(dialogFeature);
        }, nullptr);
      });
    }
  }
  if (f.layout >= 0) {
    lv_obj_t *row = mk_box(card, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_width(row, INNER_W);
    set_flex(row, LV_FLEX_FLOW_ROW, 10);
    lv_obj_t *l = mk_label(row, &font_14b, t.muted, kLayoutRow);
    lv_obj_set_width(l, 70);
    String names[3];
    const char *labels[3];
    for (int v = 0; v < 3; v++) {
      names[v] = strf(kVariant, kLetters[v], kVariants[f.layout][v]);
      labels[v] = names[v].c_str();
    }
    mk_segmented(row, labels, 3, g_settings.layouts[f.layout], (INNER_W - 80 - 4) / 3, [](int v, void *) {
      lv_async_call([](void *p) {
        flushPending();
        Settings s = g_settings;
        s.layouts[kFeatures[dialogFeature].layout] = (uint8_t)(intptr_t)p;
        ui_save_settings(s);
        showDialog(dialogFeature);
      }, (void *)(intptr_t)v);
    });
  }
  lv_obj_t *done = mk_text_button(card, kDone, true, INNER_W);
  on_click(done, [](lv_event_t *) {
    flushPending();
    dialogSub = nullptr;
    close_overlay(&dialog);
    features_refresh();
  });
}

}  // namespace

lv_obj_t *features_create() {
  const Theme &t = theme();
  lv_obj_t *scr = mk_screen();
  lv_obj_t *h = mk_back_header(scr, kTitle, Screen::Settings);
  countLabel = mk_label(h, &font_14b, t.accent, "");

  body = mk_box(scr, 0, 0);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_pos(body, PAD, BODY_Y);
  lv_obj_set_size(body, SCREEN_W - 2 * PAD, BODY_H);
  set_flex(body, LV_FLEX_FLOW_COLUMN, TILE_GAP, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  return scr;
}

void features_refresh() {
  lv_label_set_text(countLabel, strf(kOnCount, features_on_count(), kFeatureCount).c_str());
  lv_obj_clean(body);
  buildGrid();
}

int features_on_count() {
  int on = 0;
  for (int i = 0; i < kFeatureCount; i++) on += isOn(i);
  return on;
}
