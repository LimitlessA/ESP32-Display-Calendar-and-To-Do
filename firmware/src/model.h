#pragma once

#include <Arduino.h>
#include <vector>

// One calendar occurrence from an account's Apps Script feed.
struct Event {
  uint8_t account;   // index into Settings::accounts
  String key;        // occurrence id, used to dismiss it
  int64_t start;     // epoch ms
  int64_t end;       // epoch ms (exclusive; all-day events end at midnight)
  bool allDay;
  String title;
  String calendar;
  uint32_t color;    // 0xRRGGBB
  String location;
  String description;
  String organizer;
  uint16_t guests;
  uint16_t travelMin = 0;  // travel time from home for leave-by alerts, 0 = unknown
};

struct Task {
  uint8_t account;
  String id;
  String listId;
  String title;
  String list;
  String dueKey;     // yyyy-MM-dd, empty if no due date
  String notes;
  String parent;     // parent task id for subtasks
};

// A Google Tasks list, for picking where quick add puts a task.
struct TaskList {
  uint8_t account;
  String id;
  String title;
};

// Birthday, public holiday or starred event, for countdowns.
enum class DateKind : uint8_t { Star, Birthday, Holiday };
struct DateItem {
  DateKind kind;
  String title;
  String day;        // yyyy-MM-dd
  String key;        // starred: the event key
  uint8_t account;
};

struct CalendarInfo {
  String id;
  String name;
  uint32_t color;
  bool shown;
};

struct AccountStatus {
  String email;      // empty until the first successful sync
  time_t syncedAt = 0;
  String error;
  std::vector<CalendarInfo> calendars;
};

struct Snapshot {
  std::vector<Event> events;          // next 7 days, all accounts merged, by start
  std::vector<Task> tasks;            // all accounts, dated first then undated
  std::vector<AccountStatus> accounts;  // same order as Settings::accounts
  std::vector<TaskList> lists;        // every account's task lists
  std::vector<DateItem> dates;        // next 60 days, by day, duplicates across accounts removed
  std::vector<String> frequent;       // most often completed task titles, for quick add

  time_t weekStart = 0;               // local midnight of the week view's Monday
  std::vector<Event> week;            // events for that week
  bool weekLoading = false;

  uint32_t version = 0;               // bumps on every change
  time_t syncedAt = 0;                // last sync where every account succeeded
  bool offline = false;               // showing data loaded from the SD card
  String error;                       // last problem to show, empty if none
};
