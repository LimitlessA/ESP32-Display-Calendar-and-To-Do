// Desktop build of the UI: renders every screen with sample data to PPM
// files, so the layout can be checked without the board.
//   usage: sim <output dir>
#include <lvgl.h>
#include <stdio.h>

#include "ota.h"
#include "settings.h"
#include "sync.h"
#include "ui.h"
#include "ui_internal.h"

Snapshot g_sim;
time_t g_sim_now;
extern OtaStatus g_sim_ota;
void sim_set_settings(const Settings &s);

static lv_color_t fb[SCREEN_W * SCREEN_H];
static lv_color_t buf[SCREEN_W * SCREEN_H];

static void flush(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *px) {
  for (int y = a->y1; y <= a->y2; y++) {
    memcpy(&fb[y * SCREEN_W + a->x1], px, (a->x2 - a->x1 + 1) * sizeof(lv_color_t));
    px += a->x2 - a->x1 + 1;
  }
  lv_disp_flush_ready(drv);
}

static time_t at(int y, int mo, int d, int h, int mi) {
  struct tm tm = {};
  tm.tm_year = y - 1900;
  tm.tm_mon = mo - 1;
  tm.tm_mday = d;
  tm.tm_hour = h;
  tm.tm_min = mi;
  tm.tm_isdst = -1;
  return mktime(&tm);
}

static Event ev(uint8_t acc, const char *key, time_t s, time_t e, const char *title, const char *cal, uint32_t color,
                bool allDay = false) {
  Event x;
  x.account = acc;
  x.key = key;
  x.start = int64_t(s) * 1000;
  x.end = int64_t(e) * 1000;
  x.allDay = allDay;
  x.title = title;
  x.calendar = cal;
  x.color = color;
  x.guests = 0;
  return x;
}

static Task task(uint8_t acc, const char *id, const char *title, const char *list, const char *due,
                 const char *parent = "") {
  Task t;
  t.account = acc;
  t.id = id;
  t.listId = list;
  t.title = title;
  t.list = list;
  t.dueKey = due;
  t.parent = parent;
  return t;
}

static void sampleData() {
  const uint32_t P = 0x7BD88F, W = 0x6C8CFF, F = 0xF5A524;
  Event review = ev(1, "review", at(2026, 10, 29, 14, 0), at(2026, 10, 29, 15, 0), "Design review", "Work", W);
  review.location = "Room 4B, 2nd floor";
  review.organizer = "maria.pop@company.com";
  review.guests = 6;
  review.description = "Walk through the new onboarding screens and agree on what ships in the November release. "
                       "Bring the latest prototype.";
  g_sim.events = {
      ev(0, "lunch", at(2026, 10, 29, 12, 30), at(2026, 10, 29, 13, 15), "Lunch with Ana", "Personal", P),
      review,
      ev(0, "bank", at(2026, 10, 29, 17, 30), at(2026, 10, 29, 18, 0), "Call with the bank", "Personal", P),
      ev(0, "bday", at(2026, 10, 30, 0, 0), at(2026, 10, 31, 0, 0), "Mihai\xE2\x80\x99s birthday", "Family", F, true),
      ev(1, "standup30", at(2026, 10, 30, 9, 30), at(2026, 10, 30, 10, 0), "Team standup", "Work", W),
      ev(1, "maria", at(2026, 10, 30, 13, 0), at(2026, 10, 30, 13, 30), "1:1 with Maria", "Work", W),
      ev(0, "football", at(2026, 10, 31, 11, 0), at(2026, 10, 31, 13, 0), "Football with friends", "Personal", P),
      ev(0, "parents", at(2026, 11, 1, 19, 0), at(2026, 11, 1, 21, 0), "Dinner at parents", "Family", F),
      ev(0, "dentist", at(2026, 11, 2, 10, 0), at(2026, 11, 2, 10, 45), "Dentist", "Personal", P),
  };
  g_sim.week = {
      ev(1, "s26", at(2026, 10, 26, 9, 30), at(2026, 10, 26, 10, 0), "Team standup", "Work", W),
      ev(0, "gym", at(2026, 10, 26, 18, 0), at(2026, 10, 26, 19, 0), "Gym", "Personal", P),
      ev(1, "s27", at(2026, 10, 27, 9, 30), at(2026, 10, 27, 10, 0), "Team standup", "Work", W),
      ev(1, "plan", at(2026, 10, 27, 15, 0), at(2026, 10, 27, 16, 0), "Quarterly planning", "Work", W),
      ev(0, "book", at(2026, 10, 28, 19, 0), at(2026, 10, 28, 21, 0), "Book club", "Family", F),
  };
  for (const Event &e : g_sim.events) {
    if (e.start / 1000 < at(2026, 11, 2, 0, 0)) g_sim.week.push_back(e);
  }
  g_sim.tasks = {
      task(0, "t1", "Renew car insurance", "My Tasks", "2026-10-28"),
      task(1, "t2", "Send invoice to client", "Work", "2026-10-29"),
      task(0, "t3", "Book flights for December", "My Tasks", "2026-10-31"),
      task(0, "t4", "Compare prices on two airlines", "My Tasks", "", "t3"),
      task(0, "t5", "Order ESP32 enclosure", "Projects", ""),
      task(1, "t6", "Prepare slides for the review", "Work", ""),
      task(0, "t7", "Buy printer ink", "Shopping", ""),
  };
  AccountStatus personal, work;
  personal.email = "andrei@gmail.com";
  personal.syncedAt = at(2026, 10, 29, 12, 43);
  personal.calendars = {{"p", "Personal", P, true}, {"f", "Family", F, true}, {"h", "Holidays in Romania", 0x4FD1C5, false}};
  work.email = "andrei@company.com";
  work.syncedAt = personal.syncedAt;
  work.calendars = {{"w", "Work", W, true}, {"o", "Office rooms", 0xE57CD8, false}};
  g_sim.accounts = {personal, work};
  g_sim.syncedAt = personal.syncedAt;
  g_sim.version = 1;
}

static void render(const char *dir, const char *name) {
  for (int i = 0; i < 5; i++) lv_timer_handler();
  lv_obj_invalidate(lv_scr_act());
  lv_obj_invalidate(lv_layer_top());
  lv_refr_now(nullptr);
  char path[512];
  snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
  FILE *f = fopen(path, "wb");
  fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
  for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
    const lv_color_t p = fb[i];
    const uint8_t rgb[3] = {(uint8_t)(p.ch.red << 3 | p.ch.red >> 2), (uint8_t)(p.ch.green << 2 | p.ch.green >> 4),
                            (uint8_t)(p.ch.blue << 3 | p.ch.blue >> 2)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
  printf("wrote %s\n", path);
}

static void clearTop() {
  for (int i = 0; i < 5; i++) lv_timer_handler();  // let pending async deletes run first
  lv_obj_clean(lv_layer_top());
}

int main(int argc, char **argv) {
  const char *dir = argc > 1 ? argv[1] : ".";
  setenv("TZ", "UTC-2", 1);
  tzset();
  g_sim_now = at(2026, 10, 29, 12, 45);

  lv_init();
  static lv_disp_draw_buf_t draw;
  lv_disp_draw_buf_init(&draw, buf, nullptr, SCREEN_W * SCREEN_H);
  static lv_disp_drv_t drv;
  lv_disp_drv_init(&drv);
  drv.hor_res = SCREEN_W;
  drv.ver_res = SCREEN_H;
  drv.flush_cb = flush;
  drv.draw_buf = &draw;
  lv_disp_drv_register(&drv);

  Settings s;
  s.accounts = {{"Personal", "https://script.google.com/macros/s/a/exec", "x", 0x7BD88F},
                {"Work", "https://script.google.com/macros/s/b/exec", "y", 0x6C8CFF}};
  s.wifiSsid = "Popescu-5G";
  sim_set_settings(s);
  sampleData();

  ui_init();
  Snapshot snap;
  if (sync_get_if_newer(0, snap)) ui_update(snap);
  ui_tick();
  clearTop();
  render(dir, "01-home");

  for (const Event &e : g_sim.events) {
    if (e.key == "review") ui_open_event(e);
  }
  render(dir, "02-details");
  ui_show(Screen::Week);
  if (sync_get_if_newer(snap.version, snap)) ui_update(snap);
  render(dir, "03-week");
  ui_show(Screen::Tasks);
  render(dir, "04-tasks");

  ui_show(Screen::Home);
  reminder_show(g_sim.events[1]);
  render(dir, "05-reminder");
  reminder_hide();
  clearTop();

  ui_show(Screen::Night);
  g_sim_now = at(2026, 10, 29, 23, 18);
  ui_tick();
  render(dir, "06-night");
  g_sim_now = at(2026, 10, 29, 12, 45);
  ui_tick();

  ui_show(Screen::Wifi);
  wifi_tick();
  render(dir, "07-wifi");
  ui_show(Screen::Accounts);
  render(dir, "08-accounts");
  ui_show(Screen::Settings);
  render(dir, "09-settings");

  theme_picker_show();
  render(dir, "10-theme-picker");
  clearTop();

  g_sim_ota.state = OtaState::Available;
  g_sim_ota.latest = "1.1.0";
  ota_dialog_show();
  render(dir, "11-update");
  clearTop();

  const char *names[] = {"midnight", "ocean", "forest", "sunset", "paper", "high-contrast"};
  for (uint8_t i = 1; i < kThemeCount; i++) {
    Settings t = g_settings;
    t.theme = i;
    ui_save_settings(t);
    ui_show(Screen::Home);
    char name[64];
    snprintf(name, sizeof(name), "theme-%d-%s", i, names[i]);
    render(dir, name);
  }
  return 0;
}
