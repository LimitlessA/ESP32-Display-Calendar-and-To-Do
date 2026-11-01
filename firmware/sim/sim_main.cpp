// Desktop build of the UI: renders every screen with sample data to PPM
// files, so the layout can be checked without the board.
//   usage: sim <output dir>
#include <lvgl.h>
#include <stdio.h>

#include "ota.h"
#include "desk.h"
#include "presence.h"
#include "settings.h"
#include "sync.h"
#include "ui.h"
#include "ui_internal.h"

Snapshot g_sim;
time_t g_sim_now;
extern OtaStatus g_sim_ota;
extern uint32_t g_sim_ms;
extern PhoneState g_sim_phone;
extern long g_sim_seen_ago;
extern DeskStatus g_sim_desk;
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
      ev(1, "client", at(2026, 10, 29, 14, 30), at(2026, 10, 29, 15, 0), "Client call", "Work", W),
      ev(0, "bank", at(2026, 10, 29, 17, 30), at(2026, 10, 29, 18, 0), "Call with the bank", "Personal", P),
      ev(0, "bday", at(2026, 10, 30, 0, 0), at(2026, 10, 31, 0, 0), "Mihai\xE2\x80\x99s birthday", "Family", F, true),
      ev(1, "standup30", at(2026, 10, 30, 9, 30), at(2026, 10, 30, 10, 0), "Team standup", "Work", W),
      ev(1, "maria", at(2026, 10, 30, 13, 0), at(2026, 10, 30, 13, 30), "1:1 with Maria", "Work", W),
      ev(0, "football", at(2026, 10, 31, 11, 0), at(2026, 10, 31, 13, 0), "Football with friends", "Personal", P),
      ev(0, "parents", at(2026, 11, 1, 19, 0), at(2026, 11, 1, 21, 0), "Dinner at parents", "Family", F),
      ev(0, "dentist", at(2026, 11, 2, 10, 0), at(2026, 11, 2, 10, 45), "Dentist", "Personal", P),
  };
  Event car = ev(0, "car", at(2026, 10, 29, 13, 30), at(2026, 10, 29, 14, 0), "Pick up the car", "Personal", P);
  car.location = "Service Auto, Bd. Pipera 1";
  car.travelMin = 40;
  g_sim.events.insert(g_sim.events.begin() + 1, car);
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
      task(0, "t8", "Pay electricity bill", "My Tasks", "2026-10-29"),
  };
  g_sim.lists = {{0, "l1", "My Tasks"}, {1, "l2", "Work"}, {0, "l3", "Shopping"}, {0, "l4", "Projects"}};
  g_sim.dates = {
      {DateKind::Birthday, "Mihai's birthday", "2026-10-30", "", 0},
      {DateKind::Birthday, "Ana's birthday", "2026-11-08", "", 0},
      {DateKind::Star, "Trip to Rome", "2026-11-12", "rome", 0},
      {DateKind::Holiday, "St Andrew's Day", "2026-11-30", "", 0},
      {DateKind::Holiday, "National Day", "2026-12-01", "", 0},
      {DateKind::Star, "Christmas party", "2026-12-18", "xmas", 1},
  };
  g_sim.frequent = {"Buy milk", "Water the plants", "Take out the recycling", "Call mom", "Pay electricity bill",
                    "Gym", "Clean the kitchen", "Order groceries", "Back up photos", "Check car tires",
                    "Read 20 pages", "Plan the week"};
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
  lv_obj_invalidate(lv_layer_sys());
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

static void tick() {
  g_sim_ms += 6000;  // past every throttle in ui_tick
  ui_tick();
  Snapshot snap;
  if (sync_get_if_newer(g_snap.version, snap)) ui_update(snap);
}

static void setLayout(Layout l, uint8_t v) {
  Settings s = g_settings;
  for (uint8_t i = 0; i < kLayoutCount; i++) s.layouts[i] = kDefaultLayouts[i];
  s.layouts[(int)l] = v;
  ui_save_settings(s);
}

static const Task &taskById(const char *id) {
  for (const Task &t : g_snap.tasks) {
    if (t.id == id) return t;
  }
  return g_snap.tasks[0];
}

// Every A/B/C variant from the mockups, named like the mockup rows.
static void renderVariants(const char *dir) {
  const char abc[] = {'a', 'b', 'c'};
  char name[64];
  for (uint8_t v = 0; v < 3; v++) {
    snprintf(name, sizeof(name), "r1-quickadd-%c", abc[v]);
    setLayout(Layout::QuickAdd, v);
    ui_show(v == 2 ? Screen::Tasks : Screen::Home);
    quick_add_show();
    render(dir, name);
    clearTop();
  }
  for (uint8_t v = 0; v < 3; v++) {
    snprintf(name, sizeof(name), "r2-focus-%c", abc[v]);
    setLayout(Layout::Focus, v);
    ui_show(Screen::Home);
    focus_start(taskById("t2"));
    g_sim_now += 7 * 60 + 12;
    tick();
    if (v == 2) clearTop();  // the pill in the header; drop the toast
    render(dir, name);
    g_sim_now -= 7 * 60 + 12;
    focus_stop();
    clearTop();
  }
  for (uint8_t v = 0; v < 3; v++) {
    snprintf(name, sizeof(name), "r3-summary-%c", abc[v]);
    setLayout(Layout::Summary, v);
    summary_show();
    render(dir, name);
    clearTop();
  }
  for (uint8_t v = 0; v < 3; v++) {
    snprintf(name, sizeof(name), "r4-conflict-%c", abc[v]);
    setLayout(Layout::Conflicts, v);
    if (v == 0) ui_show(Screen::Home);
    if (v == 1) {
      ui_show(Screen::Home);
      const time_t now = ui_now();
      conflict_popup_show(find_conflicts(g_snap.events, now, now + 86400)[0]);
    }
    if (v == 2) {
      ui_show(Screen::Week);
      tick();
    }
    render(dir, name);
    clearTop();
  }
  for (uint8_t v = 0; v < 3; v++) {
    snprintf(name, sizeof(name), "r5-leave-%c", abc[v]);
    // A new key each time, so the alert fires again.
    for (Event &e : g_sim.events) {
      if (e.title == "Pick up the car") e.key = String("car") + abc[v];
    }
    g_sim.version++;
    setLayout(Layout::LeaveBy, v);
    ui_show(Screen::Home);
    tick();
    tick();
    render(dir, name);
    clearTop();
  }
  for (const Event &e : g_snap.events) {
    if (e.title == "Pick up the car") ui_open_event(e);
  }
  render(dir, "r5-leave-details");
  for (uint8_t v = 0; v < 3; v++) {
    snprintf(name, sizeof(name), "r7-dates-%c", abc[v]);
    setLayout(Layout::Dates, v);
    ui_show(v == 2 ? Screen::Dates : Screen::Home);
    render(dir, name);
  }
  for (uint8_t v = 0; v < 3; v++) {
    snprintf(name, sizeof(name), "r8-review-%c", abc[v]);
    setLayout(Layout::Review, v);
    review_show();
    render(dir, name);
    clearTop();
  }
  for (uint8_t v = 0; v < 3; v++) {
    snprintf(name, sizeof(name), "r9-features-%c", abc[v]);
    setLayout(Layout::Features, v);
    ui_show(Screen::Features);
    render(dir, name);
  }
  setLayout(Layout::Features, kDefaultLayouts[(int)Layout::Features]);
  layout_picker_show();
  render(dir, "r9-layouts");
  clearTop();

  // Round 4. During the design review (14:00-15:00, 6 people).
  const time_t before = g_sim_now;
  g_sim_now = at(2026, 10, 29, 14, 38);
  Settings cost = g_settings;
  cost.costMeter = true;
  ui_save_settings(cost);
  for (uint8_t v = 0; v < 3; v++) {
    snprintf(name, sizeof(name), "r10-meeting-%c", abc[v]);
    setLayout(Layout::MeetTimer, v);
    Settings s = g_settings;
    s.layouts[(int)Layout::Cost] = 2;  // no cost on the card or the full screen
    ui_save_settings(s);
    ui_show(Screen::Home);
    tick();
    render(dir, name);
    clearTop();
  }
  for (uint8_t v = 0; v < 3; v++) {
    snprintf(name, sizeof(name), "r11-cost-%c", abc[v]);
    setLayout(Layout::Cost, v);
    Settings s = g_settings;
    s.layouts[(int)Layout::MeetTimer] = 2;
    ui_save_settings(s);
    ui_show(v == 2 ? Screen::Week : Screen::Home);
    tick();
    tick();  // the glowing edge of the timer stays on the system layer
    if (v == 2) meeting_receipt_show();
    render(dir, name);
    clearTop();
  }
  Settings noCost = g_settings;
  noCost.costMeter = false;
  noCost.meetTimer = false;
  ui_save_settings(noCost);
  tick();
  noCost.meetTimer = true;
  ui_save_settings(noCost);
  g_sim_now = before;

  // Phone presence: away for 40 minutes, a meeting was added meanwhile.
  for (uint8_t v = 0; v < 3; v++) {
    snprintf(name, sizeof(name), "r12-presence-%c", abc[v]);
    setLayout(Layout::Presence, v);
    Settings s = g_settings;
    s.presence = true;
    ui_save_settings(s);
    ui_show(Screen::Home);
    g_sim_phone = PhoneState::Near;
    tick();
    g_sim_phone = PhoneState::Away;
    g_sim_seen_ago = 600;
    tick();
    if (v == 0) {
      g_sim.events.push_back(ev(1, "new1", at(2026, 10, 30, 16, 0), at(2026, 10, 30, 16, 30), "Budget sync", "Work",
                                0x6C8CFF));
      g_sim.version++;
      tick();
      g_sim_now += 8 * 60;  // away 18 min in all, before the next reminder
      g_sim_phone = PhoneState::Near;
      tick();
      g_sim_now -= 8 * 60;
    }
    if (v == 2) ui_show(Screen::Night);
    render(dir, name);
    clearTop();
    g_sim_phone = PhoneState::Near;
    tick();
  }
  Settings noPresence = g_settings;
  noPresence.presence = false;
  ui_save_settings(noPresence);

  // Procrastination radar: "Book flights" pushed back five times, slides three.
  for (int i = 1; i <= 5; i++) {
    for (Task &t : g_sim.tasks) {
      if (t.id == "t3") t.dueKey = strf("2026-11-%02d", i);
      if (t.id == "t6" && i <= 4) t.dueKey = strf("2026-11-%02d", i);
    }
    g_sim.version++;
    tick();
  }
  for (uint8_t v = 0; v < 3; v++) {
    snprintf(name, sizeof(name), "r13-radar-%c", abc[v]);
    setLayout(Layout::Radar, v);
    ui_show(v == 0 ? Screen::Home : Screen::Tasks);
    if (v == 1) radar_nudge_show(taskById("t3"));
    if (v == 2) radar_view_show();
    render(dir, name);
    clearTop();
  }
  ui_show(Screen::Tasks);
  setLayout(Layout::Radar, 0);
  render(dir, "r13-radar-tasks");

  // Smart desk: a morning of sitting and standing, so the timeline has history.
  Settings desk = g_settings;
  desk.desk = true;
  ui_save_settings(desk);
  const time_t morning = g_sim_now;
  ui_show(Screen::Settings);  // fewer screens to redraw while time runs
  for (time_t t = at(2026, 10, 29, 8, 30); t < morning; t += 60) {
    g_sim_now = t;
    const int m = (int)((t - at(2026, 10, 29, 8, 30)) / 60);
    g_sim_desk.heightMm = (m % 70) >= 45 ? 1120 : 720;
    g_sim_ms += 600;
    desk_tick();
  }
  g_sim_now = morning;
  g_sim_desk.heightMm = 802;
  for (uint8_t v = 0; v < 3; v++) {
    snprintf(name, sizeof(name), "r15-desk-%c", abc[v]);
    setLayout(Layout::Desk, v);
    ui_show(v == 0 ? Screen::Home : Screen::Desk);
    tick();
    render(dir, name);
    clearTop();
  }
  ui_show(Screen::Features);
  render(dir, "r15-desk-features");
  desk_settings_show();
  tick();
  render(dir, "r15-desk-settings");
  clearTop();
  desk.desk = false;
  ui_save_settings(desk);

  for (uint8_t v = 0; v < 3; v++) {
    snprintf(name, sizeof(name), "r14-roulette-%c", abc[v]);
    setLayout(Layout::Roulette, v);
    roulette_show();
    if (v == 0) {
      for (int i = 0; i < 5; i++) lv_timer_handler();
      render(dir, "r14-roulette-a-start");
      // Press Spin: the last row of the box on the right.
      lv_obj_t *box = lv_obj_get_child(lv_obj_get_child(lv_layer_top(), 0), 3);
      lv_obj_t *row = lv_obj_get_child(box, lv_obj_get_child_cnt(box) - 1);
      lv_event_send(lv_obj_get_child(row, 0), LV_EVENT_CLICKED, nullptr);
    }
    for (int i = 0; i < 40; i++) {
      g_sim_ms += 100;
      lv_timer_handler();
    }
    render(dir, name);
    clearTop();
  }
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
  Settings base = g_settings;
  base.theme = 0;
  ui_save_settings(base);
  renderVariants(dir);
  return 0;
}
