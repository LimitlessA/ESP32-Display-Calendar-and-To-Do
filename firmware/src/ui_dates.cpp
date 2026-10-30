// Countdowns to starred events, birthdays and public holidays (mockup row 7):
// A a column on Home, B a ribbon under the header, C a Dates tab.
#include "sync.h"
#include "ui_internal.h"

namespace {

// ---- Texts and settings ------------------------------------------------------

constexpr const char *kColDates = "Dates";
constexpr const char *kTodayWord = "Today";
constexpr const char *kTomorrowWord = "Tomorrow";
constexpr const char *kInDays = "in %ld days";
constexpr const char *kDays = "days";
constexpr const char *kDay = "day";
constexpr const char *kCountdowns = "COUNTDOWNS";
constexpr const char *kBirthdays = "BIRTHDAYS";
constexpr const char *kHolidays = "PUBLIC HOLIDAYS";
constexpr const char *kNothing = "Nothing coming up. Star a meeting in its details to count down to it.";
constexpr const char *kNothingShort = "Star a meeting to count down to it";
constexpr const char *kStarHint = "Starred events, birthdays from Contacts and public holidays for the next 60 days.";
constexpr int kColumnItems = 5;    // A
constexpr int kRibbonItems = 3;    // B, as many as fit the width
constexpr int RIBBON_H = 36;
constexpr int CARD_W = (SCREEN_W - 2 * PAD - 2 * GAP) / 3;

// ---- State -------------------------------------------------------------------

lv_obj_t *datesBody = nullptr;   // Dates screen (C)

// ---- Code --------------------------------------------------------------------

std::vector<DateItem> visible() {
  std::vector<DateItem> out;
  for (const DateItem &d : g_snap.dates) {
    const bool on = d.kind == DateKind::Star ? g_settings.countdowns : g_settings.dates;
    if (on && days_until(d.day) >= 0) out.push_back(d);
  }
  return out;
}

const char *iconOf(DateKind k) {
  return k == DateKind::Star ? ICON_STAR : k == DateKind::Birthday ? ICON_GIFT : ICON_FLAG;
}

uint32_t colorOf(DateKind k) {
  const Theme &t = theme();
  return k == DateKind::Star ? t.accent : k == DateKind::Birthday ? t.badFg : t.good;
}

String whenText(long days) {
  if (days == 0) return kTodayWord;
  if (days == 1) return kTomorrowWord;
  return strf(kInDays, days);
}

lv_obj_t *transparent(lv_obj_t *parent) {
  lv_obj_t *o = mk_box(parent, 0, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  return o;
}

// Card with the number of days on the left: used by A and C.
lv_obj_t *countCard(lv_obj_t *parent, const DateItem &d, int w, bool large) {
  const Theme &t = theme();
  const long days = days_until(d.day);
  lv_obj_t *card = mk_box(parent, t.card, 12);
  lv_obj_set_size(card, w, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(card, large ? 14 : 10, 0);
  set_flex(card, LV_FLEX_FLOW_ROW, large ? 14 : 10);
  lv_obj_t *num = transparent(card);
  lv_obj_set_width(num, large ? 64 : 46);
  set_flex(num, LV_FLEX_FLOW_COLUMN, 0, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  if (days == 0) {
    mk_label(num, large ? &font_20b : &font_16s, colorOf(d.kind), iconOf(d.kind));
  } else {
    mk_label(num, large ? &font_34b : &font_20b, colorOf(d.kind), String(days).c_str());
    mk_label(num, &font_12, t.muted, days == 1 ? kDay : kDays);
  }
  lv_obj_t *col = transparent(card);
  lv_obj_set_flex_grow(col, 1);
  set_flex(col, LV_FLEX_FLOW_COLUMN, 2, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_t *title = mk_label(col, large ? &font_16s : &font_14b, t.text, d.title.c_str());
  lv_obj_set_width(title, w - (large ? 28 + 64 + 14 : 20 + 46 + 10));
  lv_label_set_long_mode(title, large ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_DOT);
  const time_t day = ui_now() + days * 86400L;
  String sub = String(iconOf(d.kind)) + "  " + (days < 2 ? whenText(days) : fmt_date_short(day));
  mk_label(col, &font_12, t.muted, sub.c_str());
  return card;
}

void section(lv_obj_t *parent, const char *title, DateKind kind, const std::vector<DateItem> &all) {
  bool any = false;
  for (const DateItem &d : all) any |= d.kind == kind;
  if (!any) return;
  const Theme &t = theme();
  lv_obj_t *head = transparent(parent);
  lv_obj_set_width(head, SCREEN_W - 2 * PAD);
  set_flex(head, LV_FLEX_FLOW_ROW, 8);
  mk_label(head, &font_14, colorOf(kind), iconOf(kind));
  lv_obj_t *h = mk_label(head, &font_12, t.muted, title);
  lv_obj_set_style_text_letter_space(h, 1, 0);
  lv_obj_t *grid = transparent(parent);
  lv_obj_set_width(grid, SCREEN_W - 2 * PAD);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_row(grid, GAP, 0);
  lv_obj_set_style_pad_column(grid, GAP, 0);
  for (const DateItem &d : all) {
    if (d.kind == kind) countCard(grid, d, CARD_W, true);
  }
}

}  // namespace

bool dates_any() { return !visible().empty(); }

void mk_dates_column(lv_obj_t *parent, int w) {
  const Theme &t = theme();
  lv_obj_t *head = transparent(parent);
  lv_obj_set_size(head, w, 30);
  lv_obj_set_style_pad_hor(head, 2, 0);
  mk_label(head, &font_14b, t.text, kColDates);
  int n = 0;
  for (const DateItem &d : visible()) {
    if (n++ >= kColumnItems) break;
    countCard(parent, d, w, false);
  }
  if (!n) {
    lv_obj_t *empty = mk_box(parent, t.card, 12);
    lv_obj_set_width(empty, w);
    lv_obj_set_style_pad_all(empty, 12, 0);
    lv_obj_t *l = mk_label(empty, &font_12, t.muted, kNothingShort);
    lv_obj_set_width(l, w - 24);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  }
}

lv_obj_t *mk_dates_ribbon(lv_obj_t *parent, int w) {
  if (g_settings.layout(Layout::Dates) != 1) return nullptr;
  const std::vector<DateItem> all = visible();
  if (all.empty()) return nullptr;
  const Theme &t = theme();
  lv_obj_t *ribbon = transparent(parent);
  lv_obj_set_size(ribbon, w, RIBBON_H);
  set_flex(ribbon, LV_FLEX_FLOW_ROW, 8);
  lv_obj_clear_flag(ribbon, LV_OBJ_FLAG_SCROLLABLE);
  int n = 0;
  for (const DateItem &d : all) {
    if (n++ >= kRibbonItems) break;
    lv_obj_t *chip = mk_box(ribbon, t.card, RIBBON_H / 2);
    lv_obj_set_size(chip, LV_SIZE_CONTENT, RIBBON_H);
    lv_obj_set_style_pad_hor(chip, 14, 0);
    set_flex(chip, LV_FLEX_FLOW_ROW, 8);
    mk_label(chip, &font_14, colorOf(d.kind), iconOf(d.kind));
    lv_obj_t *title = mk_label(chip, &font_14b, t.text, d.title.c_str());
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_max_width(title, 150, 0);
    mk_label(chip, &font_14, t.muted, whenText(days_until(d.day)).c_str());
  }
  return ribbon;
}

lv_obj_t *dates_create() {
  lv_obj_t *scr = mk_screen();
  mk_nav_header(scr, Screen::Dates);
  datesBody = mk_scroll_column(scr, GAP);
  lv_obj_set_pos(datesBody, PAD, HEADER_H + GAP);
  lv_obj_set_size(datesBody, SCREEN_W - 2 * PAD, SCREEN_H - HEADER_H - GAP);
  lv_obj_set_style_pad_bottom(datesBody, PAD, 0);
  return scr;
}

void dates_refresh() {
  if (!datesBody) return;
  const Theme &t = theme();
  lv_obj_clean(datesBody);
  const std::vector<DateItem> all = visible();
  if (all.empty()) {
    lv_obj_t *l = mk_label(datesBody, &font_16, t.muted, kNothing);
    lv_obj_set_width(l, SCREEN_W - 2 * PAD);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    return;
  }
  if (g_settings.countdowns) section(datesBody, kCountdowns, DateKind::Star, all);
  if (g_settings.dates) {
    section(datesBody, kBirthdays, DateKind::Birthday, all);
    section(datesBody, kHolidays, DateKind::Holiday, all);
  }
  lv_obj_t *hint = mk_label(datesBody, &font_12, t.muted, kStarHint);
  lv_obj_set_style_pad_top(hint, 4, 0);
}
