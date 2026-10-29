// Night clock: dim, black, tap anywhere to wake (mockup 6).
#include "ui_internal.h"

namespace {

lv_obj_t *clockLabel, *dateLabel, *nextLabel, *tasksLabel, *footLabel;

}  // namespace

lv_obj_t *night_create() {
  const Theme &t = theme();
  lv_obj_t *scr = mk_screen();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(scr, [](lv_event_t *) { ui_show(Screen::Home); }, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *col = mk_box(scr, 0, 0);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  set_flex(col, LV_FLEX_FLOW_COLUMN, 10, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_align(col, LV_ALIGN_CENTER, 0, -10);
  clockLabel = mk_label(col, &font_150, t.nightText, "");
  dateLabel = mk_label(col, &font_20b, t.nightText, "");

  lv_obj_t *info = mk_box(col, 0, 0);
  lv_obj_set_style_bg_opa(info, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_top(info, 26, 0);
  set_flex(info, LV_FLEX_FLOW_ROW, 28);
  nextLabel = mk_label(info, &font_16, t.nightText, "");
  tasksLabel = mk_label(info, &font_16, t.nightText, "");

  footLabel = mk_label(scr, &font_14, t.nightText, "");
  lv_obj_align(footLabel, LV_ALIGN_BOTTOM_MID, 0, -18);
  return scr;
}

void night_refresh() {
  const time_t now = ui_now();
  // The 150 px font only has digits, so drop " AM"/" PM" in 12-hour mode.
  String clock = fmt_clock();
  if (clock.indexOf(' ') > 0) clock = clock.substring(0, clock.indexOf(' '));
  lv_label_set_text(clockLabel, clock.c_str());
  lv_label_set_text(dateLabel, clock_valid() ? fmt_date_long(now).c_str() : "");

  const Event *next = nullptr;
  for (const Event &ev : g_snap.events) {
    if (!ev.allDay && ev.start / 1000 > now) {
      next = &ev;
      break;
    }
  }
  if (next) {
    const time_t s = next->start / 1000;
    const long diff = day_number(s) - day_number(now);
    const String day = diff == 0 ? String("Today") : diff == 1 ? String("Tomorrow") : fmt_date_short(s);
    lv_label_set_text(nextLabel, (String(ICON_CALENDAR "  ") + day + " " + fmt_time(s) + " · " + next->title).c_str());
  } else {
    lv_label_set_text(nextLabel, ICON_CALENDAR "  No upcoming meetings");
  }

  int dueTomorrow = 0, dueToday = 0;
  for (const Task &t : g_snap.tasks) {
    Tone tone;
    const String due = due_text(t.dueKey, tone);
    dueToday += tone != Tone::None;
    dueTomorrow += due == "Tomorrow";
  }
  String tasks = ICON_TASKS "  ";
  if (dueToday) tasks += String(dueToday) + (dueToday == 1 ? " task due today" : " tasks due today");
  else tasks += String(dueTomorrow) + (dueTomorrow == 1 ? " task due tomorrow" : " tasks due tomorrow");
  lv_label_set_text(tasksLabel, tasks.c_str());

  const int end = g_settings.nightEnd;
  char buf[64];
  snprintf(buf, sizeof(buf), "Night mode until %02d:%02d · tap to wake", end / 60, end % 60);
  lv_label_set_text(footLabel, buf);
}
