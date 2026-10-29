#pragma once

#include "model.h"

// Runs Wi-Fi, NTP and the Apps Script calls on a background task so the
// screen stays responsive while HTTPS requests are in flight. Shows the last
// copy saved on the SD card until the first sync finishes.
void sync_start();

// Copies the latest data if it changed since `version`; returns false otherwise.
bool sync_get_if_newer(uint32_t version, Snapshot &out);

// Ask for a refresh now instead of waiting for the next interval.
void sync_refresh();

// Load the week view starting at this local Monday 00:00.
void sync_request_week(time_t weekStart);

// Touch actions. Each updates the on-screen data immediately, then tells
// Google in the background; if that fails the change is rolled back on the
// next sync and Snapshot::error says why.
void sync_dismiss_event(const Event &ev);
void sync_complete_task(const Task &task);
void sync_uncomplete_task(const Task &task);
void sync_show_calendar(uint8_t account, const String &calendarId, bool shown);

// Quick add: a new task in that account's list (empty listId = its default list).
void sync_add_task(uint8_t account, const String &listId, const String &title, const String &dueKey);
// Moves a task to another day (yyyy-MM-dd, empty = no date).
void sync_set_task_due(const Task &task, const String &dueKey);
// Starred events show as countdowns. dayKey is the event's local day (yyyy-MM-dd).
void sync_star_event(const Event &ev, bool on, const String &dayKey);
