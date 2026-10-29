// Meeting details (mockup 2).
#include "sync.h"
#include "ui_internal.h"

namespace {

Event shown;
lv_obj_t *dot, *calLabel, *untilLabel, *title, *whenLabel, *locRow, *locLabel;
lv_obj_t *peopleRow, *peopleLabel, *descLabel, *remindBtn, *remindLabel;

lv_obj_t *infoRow(lv_obj_t *parent, const char *icon, lv_obj_t **label) {
  const Theme &t = theme();
  lv_obj_t *row = mk_box(parent, 0, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  set_flex(row, LV_FLEX_FLOW_ROW, 12);
  lv_obj_t *i = mk_label(row, &font_16, t.muted, icon);
  lv_obj_set_width(i, 20);
  lv_obj_set_style_text_align(i, LV_TEXT_ALIGN_CENTER, 0);
  *label = mk_label(row, &font_16, t.text, "");
  lv_label_set_long_mode(*label, LV_LABEL_LONG_DOT);
  lv_obj_set_width(*label, 460);
  return row;
}

void updateRemind() {
  const int o = ui_reminder_override(shown.key);
  const bool on = o == 1 || (o < 0 && g_settings.chime);
  String text = on ? "Reminder on (" + String((int)g_settings.remindMinutes) + " min before)" : String("Remind me before");
  lv_label_set_text(remindLabel, text.c_str());
  lv_obj_set_style_text_color(remindLabel, c(on ? theme().onAccent : theme().text), 0);
  lv_obj_set_style_bg_color(remindBtn, c(on ? theme().accent : theme().raised), 0);
}

}  // namespace

lv_obj_t *detail_create() {
  const Theme &t = theme();
  lv_obj_t *scr = mk_screen();
  mk_back_header(scr, "Back", Screen::Home);

  lv_obj_t *card = mk_box(scr, t.card, 18);
  lv_obj_set_pos(card, PAD, HEADER_H + PAD);
  lv_obj_set_size(card, SCREEN_W - 2 * PAD - 220 - PAD, SCREEN_H - HEADER_H - 2 * PAD);
  lv_obj_set_style_pad_hor(card, 24, 0);
  lv_obj_set_style_pad_ver(card, 22, 0);
  set_flex(card, LV_FLEX_FLOW_COLUMN, 14, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  lv_obj_t *top = mk_box(card, 0, 0);
  lv_obj_set_style_bg_opa(top, LV_OPA_TRANSP, 0);
  lv_obj_set_width(top, lv_pct(100));
  set_flex(top, LV_FLEX_FLOW_ROW, 10);
  dot = mk_dot(top, t.accent, 10);
  calLabel = mk_label(top, &font_14, t.muted, "");
  lv_obj_set_flex_grow(calLabel, 1);
  untilLabel = mk_label(top, &font_14b, t.accent, "");

  title = mk_label(card, &font_34b, t.text, "");
  lv_obj_set_width(title, lv_pct(100));
  lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);

  infoRow(card, ICON_CLOCK, &whenLabel);
  locRow = infoRow(card, ICON_PIN, &locLabel);
  peopleRow = infoRow(card, ICON_USERS, &peopleLabel);

  descLabel = mk_label(card, &font_14, t.muted, "");
  lv_obj_set_width(descLabel, lv_pct(100));
  lv_label_set_long_mode(descLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_flex_grow(descLabel, 1);
  lv_obj_set_style_text_line_space(descLabel, 4, 0);

  lv_obj_t *side = mk_box(scr, 0, 0);
  lv_obj_set_style_bg_opa(side, LV_OPA_TRANSP, 0);
  lv_obj_set_pos(side, SCREEN_W - PAD - 220, HEADER_H + PAD);
  lv_obj_set_width(side, 220);
  set_flex(side, LV_FLEX_FLOW_COLUMN, 10, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  remindBtn = mk_button(side, t.raised, 14);
  lv_obj_set_size(remindBtn, 220, 64);
  lv_obj_set_style_pad_hor(remindBtn, 14, 0);
  set_flex(remindBtn, LV_FLEX_FLOW_ROW, 10);
  mk_label(remindBtn, &font_16, t.text, ICON_BELL);
  remindLabel = mk_label(remindBtn, &font_14b, t.text, "");
  lv_obj_set_flex_grow(remindLabel, 1);
  lv_label_set_long_mode(remindLabel, LV_LABEL_LONG_WRAP);
  on_click(remindBtn, [](lv_event_t *) {
    const int o = ui_reminder_override(shown.key);
    const bool on = o == 1 || (o < 0 && g_settings.chime);
    ui_set_reminder_override(shown.key, on ? 0 : 1);
    updateRemind();
  });

  lv_obj_t *dismiss = mk_button(side, t.bg, 14);
  lv_obj_set_size(dismiss, 220, 56);
  lv_obj_set_style_border_color(dismiss, c(t.line), 0);
  lv_obj_set_style_border_width(dismiss, 2, 0);
  set_flex(dismiss, LV_FLEX_FLOW_ROW, 10, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  mk_label(dismiss, &font_14, t.text, ICON_CLOSE);
  mk_label(dismiss, &font_14b, t.text, "Dismiss from display");
  on_click(dismiss, [](lv_event_t *) {
    sync_dismiss_event(shown);
    ui_show(Screen::Home);
    toast("Hidden from the display. Your calendar is unchanged.");
  });

  lv_obj_t *note = mk_label(side, &font_12, t.muted, "Dismiss only hides it here. Your Google Calendar is not changed.");
  lv_obj_set_width(note, 212);
  lv_obj_set_style_pad_left(note, 4, 0);
  lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
  return scr;
}

void detail_show(const Event &ev) {
  shown = ev;
  const String accountName = ev.account < g_settings.accounts.size() ? g_settings.accounts[ev.account].label : String("");
  lv_obj_set_style_bg_color(dot, c(ev.color), 0);
  String cal = ev.calendar;
  if (accountName.length()) cal += " · " + accountName;
  lv_label_set_text(calLabel, cal.c_str());
  lv_label_set_text(title, ev.title.c_str());
  lv_label_set_text(whenLabel, event_when(ev).c_str());

  if (ev.location.length()) {
    lv_obj_clear_flag(locRow, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(locLabel, ev.location.c_str());
  } else {
    lv_obj_add_flag(locRow, LV_OBJ_FLAG_HIDDEN);
  }
  if (ev.guests || ev.organizer.length()) {
    lv_obj_clear_flag(peopleRow, LV_OBJ_FLAG_HIDDEN);
    String people = ev.organizer.length() ? ev.organizer + " (organizer)" : String("");
    if (ev.guests > 1) people += (people.length() ? " and " : "") + String((int)ev.guests - 1) + " others";
    lv_label_set_text(peopleLabel, people.c_str());
  } else {
    lv_obj_add_flag(peopleRow, LV_OBJ_FLAG_HIDDEN);
  }
  lv_label_set_text(descLabel, ev.description.c_str());
  updateRemind();
  detail_tick();
}

void detail_tick() {
  lv_label_set_text(untilLabel, until_text(shown.start).c_str());
}
