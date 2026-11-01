/**
 * Calendar + Google Tasks feed for the ESP32 display.
 *
 * Deploy one copy of this script in every Google account you want on the
 * display, as a web app (Execute as: Me, Who has access: Anyone), then add its
 * URL and token on the display's setup page (http://calendar.local).
 *
 * All calls are GET <web app URL>?token=T[&action=...]:
 *   (no action)                        feed: upcoming events, open tasks, calendars
 *     &v=2                             compact format; events from this week's Monday
 *                                      through next week (&from=MS: the display's Monday)
 *     &since=HASH                      answer {same:true} if the feed has not changed
 *     &ics=URL (repeatable)            also read these published calendars (v=2 only)
 *   action=range&from=MS&to=MS         events in a time range (week view; &v=2 and &ics= as above)
 *   action=complete&list=L&task=ID     mark a task done
 *   action=uncomplete&list=L&task=ID   undo that
 *   action=dismiss&event=KEY&until=MS  hide one event occurrence on the display
 *   action=showcal&id=CAL&shown=0|1    choose which calendars the display shows
 *   action=add&title=T[&list=L][&due=D]  add a task (D = yyyy-MM-dd; no list = default list)
 *   action=due&list=L&task=ID&due=D    move a task to another day (empty D = no date)
 *   action=star&event=KEY&on=1|0&title=T&start=MS&allDay=0|1
 *                                      star an event so the display counts down to it
 *
 * Times are epoch milliseconds; the display formats them itself. Outlook,
 * iCloud and other calendars: add their published ICS link on the display's
 * setup page (Other calendars); this script then fetches it for the display.
 * Adding the link in Google Calendar ("Other calendars > From URL") works too,
 * but Google refreshes those only every few hours.
 */

const CONFIG = {
  TOKEN: 'change-me',  // paste the same value on the display's setup page
  DAYS_AHEAD: 7,
  MAX_EVENTS: 60,         // older displays (format 1)
  FEED_DAYS: 14,          // format 2: this week and the next, from Monday
  MAX_FEED_EVENTS: 150,
  MAX_RANGE_EVENTS: 150,
  MAX_ICS: 6,             // published calendar links per request
  MAX_ICS_EVENTS: 300,    // per link and window
  MAX_TASKS: 50,
  TASKLIST_IDS: [],    // empty = every task list

  // Leave-by alerts: travel time from this address to each meeting's location.
  // Empty = no travel times. Uses Apps Script's built-in Maps service (no key).
  HOME_ADDRESS: '',
  TRAVEL_MODE: 'driving',  // driving, walking, bicycling or transit
  TRAVEL_HOURS_AHEAD: 12,  // only meetings starting within this many hours

  // Birthdays and holidays on the display
  HOLIDAYS_CALENDAR: 'en.romanian#holiday@group.v.calendar.google.com',  // '' = none
  BIRTHDAYS: true,         // from your Google Contacts
  DATES_DAYS_AHEAD: 60,

  // Quick add: the display offers the tasks you complete most often.
  FREQUENT_TASKS: 12,
  FREQUENT_DAYS_BACK: 120,
};

const DISMISSED_KEY = 'dismissed';
const HIDDEN_CALS_KEY = 'hiddenCalendars';
const STARRED_KEY = 'starred';
const CACHE_HOURS_DATES = 6;      // birthdays, holidays and frequent tasks change slowly
const CACHE_MINUTES_TRAVEL = 30;
const CACHE_MINUTES_ICS = 10;     // published calendars are fetched at most this often

function doGet(e) {
  const p = (e && e.parameter) || {};
  if (p.token !== CONFIG.TOKEN) {
    return json_({ error: 'unauthorized' });
  }
  try {
    switch (p.action || '') {
      case '': return json_(feed_(p, icsUrls_(e)));
      case 'range': return json_(range_(Number(p.from), Number(p.to), p, icsUrls_(e)));
      case 'complete': return json_(setTaskStatus_(p.list, p.task, true));
      case 'uncomplete': return json_(setTaskStatus_(p.list, p.task, false));
      case 'dismiss': return json_(dismissEvent_(p.event, Number(p.until)));
      case 'showcal': return json_(showCalendar_(p.id, p.shown === '1'));
      case 'add': return json_(addTask_(p.title, p.list, p.due));
      case 'due': return json_(setTaskDue_(p.list, p.task, p.due));
      case 'star': return json_(starEvent_(p.event, p.on === '1', p.title, Number(p.start), p.allDay === '1'));
      default: return json_({ error: 'unknown action' });
    }
  } catch (err) {
    return json_({ error: String(err && err.message || err) });
  }
}

function feed_(p, icsUrls) {
  p = p || {};
  const v2 = Number(p.v) >= 2;
  const tz = CalendarApp.getDefaultCalendar().getTimeZone();
  const now = new Date();
  const cals = calendarList_();
  let from, to, max;
  if (v2) {
    // From this week's Monday (the display sends its own) through the next
    // week, so the week view can show both without a request of its own.
    const asked = Number(p.from);
    from = asked > now.getTime() - 8 * 86400000 && asked <= now.getTime() ? asked : startOfWeek_(now, tz);
    // (+2 h: still the whole next week when the clocks go back in between)
    to = Math.max(from + CONFIG.FEED_DAYS * 86400000 + 2 * 3600000, now.getTime() + CONFIG.DAYS_AHEAD * 86400000);
    max = CONFIG.MAX_FEED_EVENTS;
  } else {
    from = now.getTime();
    to = from + CONFIG.DAYS_AHEAD * 86400000;
    max = CONFIG.MAX_EVENTS;
  }
  const got = collectEvents_(cals, v2 ? icsUrls : [], from, to, tz);
  let events = got.events;
  if (events.length > max) {
    to = events[max].start;  // the feed then only vouches for events before this one
    events = events.slice(0, max);
  }
  addTravel_(events);
  const out = {
    account: Session.getEffectiveUser().getEmail(),
    tz: tz,
    utcOffsetMin: utcOffsetMinutes_(now, tz),
    calendars: v2 ? calendarsOut_(cals, got.ics) : cals.map(c => ({ id: c.id, name: c.name, color: c.color, shown: c.shown })),
    events: events.map(v2 ? compactEvent_ : legacyEvent_),
    tasks: getTasks_(),
    lists: taskLists_().map(l => ({ id: l.id, title: l.title })),
    dates: getDates_(tz),
    frequent: frequentTasks_(),
  };
  if (!v2) return out;
  out.from = from;
  out.to = to;
  // The display sends back the last fingerprint; if nothing changed it gets a
  // few bytes instead of the whole feed, and redraws nothing.
  out.hash = hash_(JSON.stringify(out));
  if (p.since && p.since === out.hash) return { same: true, hash: out.hash };
  return out;
}

function range_(from, to, p, icsUrls) {
  if (!(from > 0) || !(to > from) || to - from > 45 * 86400000) throw new Error('bad range');
  const v2 = Number((p || {}).v) >= 2;
  const tz = CalendarApp.getDefaultCalendar().getTimeZone();
  const cals = calendarList_();
  const got = collectEvents_(cals, v2 ? icsUrls : [], from, to, tz);
  const events = got.events.slice(0, CONFIG.MAX_RANGE_EVENTS);
  if (!v2) return { events: events.map(legacyEvent_) };
  return { calendars: calendarsOut_(cals, got.ics), events: events.map(compactEvent_) };
}

// ---- Calendar -------------------------------------------------------------

// The account's calendars that are not hidden in Google Calendar. One call;
// `shown` is the display's own choice (action=showcal).
function calendarList_() {
  const hidden = loadHiddenCalendars_();
  const out = [];
  let pageToken;
  do {
    const res = Calendar.CalendarList.list({
      maxResults: 250, pageToken: pageToken,
      fields: 'nextPageToken,items(id,summary,summaryOverride,backgroundColor,hidden)',
    });
    (res.items || []).forEach(c => {
      if (c.hidden) return;
      out.push({ id: c.id, name: c.summaryOverride || c.summary || c.id, color: c.backgroundColor || '', shown: !hidden[c.id] });
    });
    pageToken = res.nextPageToken;
  } while (pageToken);
  return out;
}

// Google calendars first, then each published (ICS) link; an event's `ci`
// is its calendar's index in calendarsOut_().
function calendarsOut_(cals, ics) {
  return cals.map(c => ({ id: c.id, name: c.name, color: c.color, shown: c.shown }))
    .concat(ics.map((x, i) => {
      const o = { id: 'ics:' + (i + 1), name: x.name, ics: i + 1, n: x.n };
      if (x.err) o.err = x.err;
      return o;
    }));
}

// Every shown calendar's events between from and to (epoch ms), sorted, plus
// how each ICS link went.
function collectEvents_(cals, icsUrls, from, to, tz) {
  const dismissed = loadDismissed_();
  const events = [];
  const seen = {};
  const add = e => {
    if (dismissed[e.key] || seen[e.key]) return;  // same event can sit in two calendars
    seen[e.key] = true;
    events.push(e);
  };
  cals.forEach((cal, ci) => {
    if (!cal.shown) return;
    try {
      googleEvents_(cal, ci, from, to, tz).forEach(add);
    } catch (err) {
      console.warn('calendar ' + cal.id + ': ' + (err && err.message || err));  // one bad calendar must not hide the rest
    }
  });
  const ics = (icsUrls || []).map((url, i) => {
    try {
      const got = icsEvents_(url, from, to, tz);
      got.events.forEach(e => {
        e.ci = cals.length + i;
        e.cal = got.name;
        add(e);
      });
      return { n: got.events.length, name: got.name, err: '' };
    } catch (err) {
      return { n: 0, name: '', err: String(err && err.message || err).substring(0, 120) };
    }
  });
  events.sort((a, b) => a.start - b.start || Number(b.allDay) - Number(a.allDay));
  return { events: events, ics: ics };
}

// One Calendar API call per calendar (per 250 events) with only the fields
// the display shows: much faster than CalendarApp, which makes a call for
// every guest list and status.
// Recurring events share one iCalUID, so an occurrence is identified by UID + start.
function googleEvents_(cal, ci, from, to, tz) {
  const out = [];
  let pageToken;
  do {
    const res = Calendar.Events.list(cal.id, {
      timeMin: new Date(from).toISOString(), timeMax: new Date(to).toISOString(),
      singleEvents: true, orderBy: 'startTime', maxResults: 250, pageToken: pageToken,
      fields: 'nextPageToken,items(iCalUID,status,eventType,summary,start,end,location,description,' +
        'creator/email,attendees(self,organizer,responseStatus))',
    });
    (res.items || []).forEach(ev => {
      if (ev.status === 'cancelled' || ev.eventType === 'workingLocation' || !ev.start || !ev.end) return;
      const attendees = ev.attendees || [];
      if (attendees.some(a => a.self && a.responseStatus === 'declined')) return;
      const allDay = !!ev.start.date;
      const start = allDay ? dayMs_(ev.start.date, tz) : new Date(ev.start.dateTime).getTime();
      const end = allDay ? dayMs_(ev.end.date, tz) : new Date(ev.end.dateTime).getTime();
      out.push({
        key: ev.iCalUID + '|' + start,
        start: start,
        end: end,
        allDay: allDay,
        title: ev.summary || '(no title)',
        ci: ci,
        cal: cal.name,
        color: cal.color,
        loc: ev.location || '',
        desc: plainText_(ev.description, 280),
        org: (ev.creator && ev.creator.email) || '',
        guests: attendees.filter(a => !a.organizer).length,
      });
    });
    pageToken = res.nextPageToken;
  } while (pageToken && out.length < 1000);
  return out;
}

// Format 2 (the display sends v=2): short keys, nothing empty, calendar by index.
function compactEvent_(e) {
  const o = { k: e.key, s: e.start, e: e.end, t: e.title, c: e.ci };
  if (e.allDay) o.a = true;
  if (e.loc) o.l = e.loc;
  if (e.desc) o.d = e.desc;
  if (e.org) o.o = e.org;
  if (e.guests) o.g = e.guests;
  if (e.travelMin) o.tr = e.travelMin;
  return o;
}

// Format 1, for displays with older firmware.
function legacyEvent_(e) {
  const o = {
    key: e.key, start: e.start, end: e.end, allDay: e.allDay, title: e.title, cal: e.cal || '',
    color: e.color || '', loc: e.loc, desc: e.desc, org: e.org, guests: e.guests,
  };
  if (e.travelMin) o.travelMin = e.travelMin;
  return o;
}

// Dismissing only hides the event on the display; your calendar is untouched.
// Entries expire once the event is over so the list never grows.
function dismissEvent_(key, until) {
  if (!key) throw new Error('missing event');
  withLock_(() => {
    const dismissed = loadDismissed_();
    dismissed[key] = until > 0 ? until : Date.now() + CONFIG.DAYS_AHEAD * 86400000;
    PropertiesService.getScriptProperties().setProperty(DISMISSED_KEY, JSON.stringify(dismissed));
  });
  return { ok: true };
}

function loadDismissed_() {
  const raw = PropertiesService.getScriptProperties().getProperty(DISMISSED_KEY);
  const all = raw ? JSON.parse(raw) : {};
  const now = Date.now();
  const live = {};
  Object.keys(all).forEach(k => { if (all[k] > now) live[k] = all[k]; });
  return live;
}

function showCalendar_(id, shown) {
  if (!id) throw new Error('missing calendar');
  withLock_(() => {
    const hidden = loadHiddenCalendars_();
    if (shown) delete hidden[id]; else hidden[id] = true;
    PropertiesService.getScriptProperties().setProperty(HIDDEN_CALS_KEY, JSON.stringify(hidden));
  });
  return { ok: true };
}

function loadHiddenCalendars_() {
  const raw = PropertiesService.getScriptProperties().getProperty(HIDDEN_CALS_KEY);
  return raw ? JSON.parse(raw) : {};
}

// ---- Tasks ----------------------------------------------------------------

function taskLists_() {
  return (Tasks.Tasklists.list({ maxResults: 100 }).items || [])
    .filter(l => !CONFIG.TASKLIST_IDS.length || CONFIG.TASKLIST_IDS.includes(l.id));
}

function getTasks_() {
  const lists = taskLists_();

  const tasks = [];
  lists.forEach(list => {
    let pageToken;
    do {
      const res = Tasks.Tasks.list(list.id, {
        showCompleted: false, showHidden: false, maxResults: 100, pageToken: pageToken,
      });
      (res.items || []).forEach(t => {
        if (t.title) tasks.push({ t: t, list: list });
      });
      pageToken = res.nextPageToken;
    } while (pageToken);
  });

  return tasks.slice(0, CONFIG.MAX_TASKS).map(({ t, list }) => ({
    id: t.id,
    listId: list.id,
    title: t.title,
    list: list.title,
    // Google Tasks stores only the date part of "due", as midnight UTC.
    dueKey: t.due ? t.due.substring(0, 10) : '',
    notes: plainText_(t.notes, 200),
    parent: t.parent || '',
  }));
}

function setTaskStatus_(listId, taskId, done) {
  if (!listId || !taskId) throw new Error('missing list or task');
  Tasks.Tasks.patch(done ? { status: 'completed' } : { status: 'needsAction', completed: null },
    listId, taskId);
  return { ok: true };
}

// Google Tasks keeps only the date of "due", as midnight UTC.
function dueValue_(day) {
  if (!day) return null;
  if (!/^\d{4}-\d{2}-\d{2}$/.test(day)) throw new Error('bad date');
  return day + 'T00:00:00.000Z';
}

function addTask_(title, listId, due) {
  title = String(title || '').trim();
  if (!title) throw new Error('missing title');
  const body = { title: title.substring(0, 200) };
  const d = dueValue_(due);
  if (d) body.due = d;
  const list = listId || (taskLists_()[0] || {}).id || '@default';
  const t = Tasks.Tasks.insert(body, list);
  CacheService.getScriptCache().remove('frequent');
  return { ok: true, id: t.id, listId: list };
}

function setTaskDue_(listId, taskId, due) {
  if (!listId || !taskId) throw new Error('missing list or task');
  Tasks.Tasks.patch({ due: dueValue_(due) }, listId, taskId);
  return { ok: true };
}

// Titles you completed most often lately, for the quick add tiles.
function frequentTasks_() {
  if (!CONFIG.FREQUENT_TASKS) return [];
  return cached_('frequent', CACHE_HOURS_DATES * 3600, () => {
    const since = new Date(Date.now() - CONFIG.FREQUENT_DAYS_BACK * 86400000).toISOString();
    const counts = {};
    taskLists_().forEach(list => {
      let pageToken, pages = 0;
      do {
        const res = Tasks.Tasks.list(list.id, {
          showCompleted: true, showHidden: true, completedMin: since, maxResults: 100, pageToken: pageToken,
        });
        (res.items || []).forEach(t => {
          if (t.status !== 'completed' || !t.title) return;
          const title = t.title.trim();
          const k = title.toLowerCase();
          counts[k] = counts[k] || { title: title, n: 0 };
          counts[k].n++;
        });
        pageToken = res.nextPageToken;
      } while (pageToken && ++pages < 5);
    });
    return Object.keys(counts).map(k => counts[k])
      .sort((a, b) => b.n - a.n || a.title.localeCompare(b.title))
      .slice(0, CONFIG.FREQUENT_TASKS).map(x => x.title);
  });
}

// ---- Travel times (leave-by alerts) ---------------------------------------

function addTravel_(events) {
  if (!CONFIG.HOME_ADDRESS) return events;
  const limit = Date.now() + CONFIG.TRAVEL_HOURS_AHEAD * 3600000;
  events.forEach(e => {
    if (e.allDay || !e.loc || e.start > limit || e.start < Date.now()) return;
    const min = travelMinutes_(e.loc, e.start);
    if (min > 0) e.travelMin = min;
  });
  return events;
}

function travelMinutes_(destination, startMs) {
  const hour = Math.floor(startMs / 3600000);  // traffic changes over the day
  const key = 'tr:' + hour + ':' + Utilities.base64EncodeWebSafe(
    Utilities.computeDigest(Utilities.DigestAlgorithm.MD5, destination));
  return cached_(key, CACHE_MINUTES_TRAVEL * 60, () => {
    try {
      const finder = Maps.newDirectionFinder()
        .setOrigin(CONFIG.HOME_ADDRESS)
        .setDestination(destination)
        .setMode(CONFIG.TRAVEL_MODE);
      // Ask for the departure that arrives in time, so traffic is counted.
      const depart = new Date(Math.max(Date.now() + 60000, startMs - 3600000));
      if (CONFIG.TRAVEL_MODE === 'driving' || CONFIG.TRAVEL_MODE === 'transit') finder.setDepart(depart);
      const route = (finder.getDirections().routes || [])[0];
      const leg = route && route.legs && route.legs[0];
      if (!leg) return 0;
      const secs = (leg.duration_in_traffic || leg.duration || {}).value || 0;
      return Math.ceil(secs / 60);
    } catch (err) {
      return 0;  // not an address Maps understands, such as "Room 4B"
    }
  });
}

// ---- Birthdays, holidays and starred events --------------------------------

function getDates_(tz) {
  const dates = cached_('dates', CACHE_HOURS_DATES * 3600, () => {
    const from = new Date();
    from.setHours(0, 0, 0, 0);
    const to = new Date(from.getTime() + CONFIG.DATES_DAYS_AHEAD * 86400000);
    const out = [];
    const add = (kind, items) => (items || []).forEach(e => {
      const day = e.start.date || (e.start.dateTime || '').substring(0, 10);
      if (day && e.summary) out.push({ kind: kind, title: e.summary, day: day, key: kind + ':' + day + ':' + e.summary });
    });
    const query = { timeMin: from.toISOString(), timeMax: to.toISOString(), singleEvents: true, orderBy: 'startTime', maxResults: 50 };
    if (CONFIG.HOLIDAYS_CALENDAR) {
      try { add('holiday', Calendar.Events.list(CONFIG.HOLIDAYS_CALENDAR, query).items); } catch (err) {}
    }
    if (CONFIG.BIRTHDAYS) {
      try {
        add('birthday', Calendar.Events.list('primary', Object.assign({ eventTypes: ['birthday'] }, query)).items);
      } catch (err) {
        try { add('birthday', Calendar.Events.list('addressbook#contacts@group.v.calendar.google.com', query).items); } catch (e2) {}
      }
    }
    return out;
  });
  const starred = loadStarred_();
  Object.keys(starred).forEach(k => {
    const s = starred[k];
    dates.push({ kind: 'star', title: s.title, day: Utilities.formatDate(new Date(s.start), tz, 'yyyy-MM-dd'), key: k });
  });
  return dates.sort((a, b) => a.day < b.day ? -1 : a.day > b.day ? 1 : 0);
}

function starEvent_(key, on, title, start, allDay) {
  if (!key) throw new Error('missing event');
  withLock_(() => {
    const starred = loadStarred_();
    if (on) starred[key] = { title: String(title || '').substring(0, 120), start: start, allDay: allDay };
    else delete starred[key];
    PropertiesService.getScriptProperties().setProperty(STARRED_KEY, JSON.stringify(starred));
  });
  return { ok: true };
}

function loadStarred_() {
  const raw = PropertiesService.getScriptProperties().getProperty(STARRED_KEY);
  const all = raw ? JSON.parse(raw) : {};
  const today = Date.now() - 86400000;
  const live = {};
  Object.keys(all).forEach(k => { if (all[k].start > today) live[k] = all[k]; });
  return live;
}

// ---- Published calendars (ICS: Outlook, iCloud, any .ics link) --------------
//
// The display sends its ICS links with every feed and week request of its
// first account (ics=URL, repeated). This script fetches them, expands simple
// recurring events (DAILY, WEEKLY, MONTHLY, YEARLY with INTERVAL, COUNT,
// UNTIL, BYDAY, BYMONTHDAY, BYMONTH, BYSETPOS, EXDATE and moved occurrences)
// inside the requested window, and returns them like Google events.
//   Outlook / Office 365: Settings > Calendar > Shared calendars > Publish a calendar > ICS link
//   iCloud: Calendar > share icon > Public Calendar > copy link (webcal://...)

const ICS_DAY = 86400000;
const ICS_WEEKDAYS = { SU: 0, MO: 1, TU: 2, WE: 3, TH: 4, FR: 5, SA: 6 };
// Outlook writes Windows time zone names; the common ones, as IANA names.
const WINDOWS_TZ = {
  'UTC': 'UTC', 'Coordinated Universal Time': 'UTC', 'GMT Standard Time': 'Europe/London',
  'Greenwich Standard Time': 'Atlantic/Reykjavik', 'W. Europe Standard Time': 'Europe/Berlin',
  'Central Europe Standard Time': 'Europe/Budapest', 'Romance Standard Time': 'Europe/Paris',
  'Central European Standard Time': 'Europe/Warsaw', 'E. Europe Standard Time': 'Europe/Chisinau',
  'GTB Standard Time': 'Europe/Bucharest', 'FLE Standard Time': 'Europe/Kiev', 'Turkey Standard Time': 'Europe/Istanbul',
  'Russian Standard Time': 'Europe/Moscow', 'Israel Standard Time': 'Asia/Jerusalem', 'Egypt Standard Time': 'Africa/Cairo',
  'South Africa Standard Time': 'Africa/Johannesburg', 'Arabian Standard Time': 'Asia/Dubai',
  'Pakistan Standard Time': 'Asia/Karachi', 'India Standard Time': 'Asia/Kolkata', 'SE Asia Standard Time': 'Asia/Bangkok',
  'China Standard Time': 'Asia/Shanghai', 'Singapore Standard Time': 'Asia/Singapore', 'Tokyo Standard Time': 'Asia/Tokyo',
  'Korea Standard Time': 'Asia/Seoul', 'W. Australia Standard Time': 'Australia/Perth',
  'E. Australia Standard Time': 'Australia/Brisbane', 'AUS Eastern Standard Time': 'Australia/Sydney',
  'New Zealand Standard Time': 'Pacific/Auckland', 'Atlantic Standard Time': 'America/Halifax',
  'Eastern Standard Time': 'America/New_York', 'Central Standard Time': 'America/Chicago',
  'Central Standard Time (Mexico)': 'America/Mexico_City', 'Mountain Standard Time': 'America/Denver',
  'US Mountain Standard Time': 'America/Phoenix', 'Pacific Standard Time': 'America/Los_Angeles',
  'Alaskan Standard Time': 'America/Anchorage', 'Hawaiian Standard Time': 'Pacific/Honolulu',
  'SA Pacific Standard Time': 'America/Bogota', 'E. South America Standard Time': 'America/Sao_Paulo',
  'Argentina Standard Time': 'America/Argentina/Buenos_Aires',
};

function icsUrls_(e) {
  const list = (e && e.parameters && e.parameters.ics) || [];
  return list.slice(0, CONFIG.MAX_ICS);
}

function icsEvents_(url, from, to, tz) {
  url = String(url || '').trim().replace(/^webcal:\/\//i, 'https://');
  if (!/^https?:\/\//i.test(url)) throw new Error('not an https:// or webcal:// link');
  return cached_('ics:' + hash_(url) + ':' + from + ':' + to, CACHE_MINUTES_ICS * 60, () => {
    const res = UrlFetchApp.fetch(url, { muteHttpExceptions: true, followRedirects: true });
    const code = res.getResponseCode();
    if (code !== 200) throw new Error('HTTP ' + code);
    const text = res.getContentText('UTF-8');
    if (text.indexOf('BEGIN:VCALENDAR') < 0) throw new Error('not a calendar (ICS) file');
    return parseIcs_(text, from, to, tz);
  });
}

// Events between from and to (epoch ms). `tz` is used for all-day events and
// for times without a time zone.
function parseIcs_(text, from, to, tz) {
  const lines = text.replace(/\r\n[ \t]/g, '').replace(/\n[ \t]/g, '').split(/\r?\n/);
  let name = '';
  const comps = [];
  let cur = null, nested = 0;
  lines.forEach(line => {
    const p = icsProp_(line);
    if (!p) return;
    const value = p.value.trim().toUpperCase();
    if (p.name === 'BEGIN') {
      if (cur) nested++;
      else if (value === 'VEVENT') cur = { exdates: [], attendees: 0 };
      return;
    }
    if (p.name === 'END') {
      if (cur && nested) nested--;
      else if (cur && value === 'VEVENT') { comps.push(cur); cur = null; }
      return;
    }
    if (!cur) {
      if (p.name === 'X-WR-CALNAME' && !name) name = icsText_(p.value);
      return;
    }
    if (nested) return;  // VALARM and the like
    if (p.name === 'EXDATE') cur.exdates.push(p);
    else if (p.name === 'ATTENDEE') cur.attendees++;
    else if (!(p.name in cur)) cur[p.name] = p;
  });

  // Occurrences moved or changed on their own (RECURRENCE-ID) replace the
  // series' occurrence at that time.
  const moved = {};
  comps.forEach(c => {
    const rid = c['RECURRENCE-ID'] && icsTime_(c['RECURRENCE-ID'], tz);
    if (rid && c.UID) {
      const uid = c.UID.value.trim();
      (moved[uid] = moved[uid] || {})[toEpoch_(rid.f, rid.tz)] = true;
    }
  });
  const events = [];
  comps.forEach(c => {
    if (events.length >= CONFIG.MAX_ICS_EVENTS) return;
    try {
      icsExpand_(c, moved, from, to, tz, events);
    } catch (err) {
      console.warn('ics event skipped: ' + (err && err.message || err));
    }
  });
  return { name: name, events: events.slice(0, CONFIG.MAX_ICS_EVENTS) };
}

function icsExpand_(c, moved, from, to, tz, out) {
  if (!c.DTSTART || (c.STATUS && /CANCELLED/i.test(c.STATUS.value))) return;
  const st = icsTime_(c.DTSTART, tz);
  if (!st) return;
  // Length in wall-clock time, so a 10:00-11:00 series stays 10:00-11:00 across DST.
  let dur = st.allDay ? ICS_DAY : 0;
  if (c.DTEND) {
    const en = icsTime_(c.DTEND, tz);
    if (en) dur = en.tz === st.tz ? en.f - st.f : toEpoch_(en.f, en.tz) - toEpoch_(st.f, st.tz);
  } else if (c.DURATION) {
    dur = icsDuration_(c.DURATION.value);
  }
  dur = Math.max(0, dur);
  const uid = c.UID ? c.UID.value.trim() : (c.SUMMARY ? c.SUMMARY.value : '') + st.f;
  const org = c.ORGANIZER ? (c.ORGANIZER.params.CN || c.ORGANIZER.value.replace(/^mailto:/i, '')) : '';
  const base = {
    allDay: st.allDay,
    title: icsText_(c.SUMMARY && c.SUMMARY.value) || '(no title)',
    loc: icsText_(c.LOCATION && c.LOCATION.value),
    desc: plainText_(icsText_(c.DESCRIPTION && c.DESCRIPTION.value), 280),
    org: org,
    guests: c.attendees,
  };
  const emit = f => {
    const start = toEpoch_(f, st.tz);
    const end = Math.max(start, toEpoch_(f + dur, st.tz));
    if (start >= to || Math.max(end, start + 1) <= from || out.length >= CONFIG.MAX_ICS_EVENTS) return;
    out.push(Object.assign({ key: 'ics:' + uid + '|' + start, start: start, end: end }, base));
  };
  if (!c.RRULE || c['RECURRENCE-ID']) {
    emit(st.f);
    return;
  }
  const skip = Object.assign({}, moved[uid] || {});
  c.exdates.forEach(p => String(p.value).split(',').forEach(v => {
    const x = icsTime_({ value: v, params: Object.assign({ TZID: st.tz }, p.params) }, tz);
    if (x) skip[toEpoch_(x.f, x.tz)] = true;
  }));
  // Wall-clock times differ from UTC by at most 14 h: a 2-day margin is plenty.
  icsRecur_(c.RRULE.value, st, from - dur - 2 * ICS_DAY, to + 2 * ICS_DAY, f => {
    if (!skip[toEpoch_(f, st.tz)]) emit(f);
  });
}

// Calls back with each occurrence's wall-clock start ("floating" ms: the
// local date and time written as if it were UTC) between lo and hi.
function icsRecur_(rule, st, lo, hi, cb) {
  const r = {};
  String(rule).split(';').forEach(kv => {
    const i = kv.indexOf('=');
    if (i > 0) r[kv.substring(0, i).trim().toUpperCase()] = kv.substring(i + 1).trim().toUpperCase();
  });
  const freq = r.FREQ;
  if (['DAILY', 'WEEKLY', 'MONTHLY', 'YEARLY'].indexOf(freq) < 0) {
    cb(st.f);  // HOURLY and finer: show the first one only
    return;
  }
  const interval = Math.max(1, parseInt(r.INTERVAL, 10) || 1);
  const count = r.COUNT ? parseInt(r.COUNT, 10) || 1 : Infinity;
  let until = Infinity;
  if (r.UNTIL) {
    const u = icsTime_({ value: r.UNTIL, params: {} }, st.tz);
    if (u) until = u.allDay ? u.f + ICS_DAY - 1 : (u.tz === 'UTC' && st.tz !== 'UTC' ? u.f + offsetMs_(u.f, st.tz) : u.f);
  }
  const list = (s, fn) => (s ? s.split(',') : []).map(x => fn(x.trim())).filter(x => x !== null);
  const rules = {
    byday: list(r.BYDAY, s => {
      const m = /^([+-]?\d{1,2})?(SU|MO|TU|WE|TH|FR|SA)$/.exec(s);
      return m ? { n: m[1] ? parseInt(m[1], 10) : 0, d: ICS_WEEKDAYS[m[2]] } : null;
    }),
    bymonthday: list(r.BYMONTHDAY, s => { const n = parseInt(s, 10); return n && Math.abs(n) <= 31 ? n : null; }),
    bysetpos: list(r.BYSETPOS, s => { const n = parseInt(s, 10); return n ? n : null; }),
  };
  const bymonth = list(r.BYMONTH, s => { const n = parseInt(s, 10); return n >= 1 && n <= 12 ? n : null; });
  const wkst = ICS_WEEKDAYS[r.WKST] !== undefined ? ICS_WEEKDAYS[r.WKST] : 1;
  const tod = ((st.f % ICS_DAY) + ICS_DAY) % ICS_DAY;
  const day0 = st.f - tod;
  const d0 = new Date(day0);
  const y0 = d0.getUTCFullYear(), m0 = d0.getUTCMonth(), dom0 = d0.getUTCDate(), dow0 = d0.getUTCDay();

  let n = 0;
  for (let p = 0; p < 20000; p++) {
    let periodStart, days;  // the period's first day and its candidate days (floating midnights)
    if (freq === 'DAILY') {
      periodStart = day0 + p * interval * ICS_DAY;
      const dow = new Date(periodStart).getUTCDay();
      days = !rules.byday.length || rules.byday.some(b => b.d === dow) ? [periodStart] : [];
    } else if (freq === 'WEEKLY') {
      periodStart = day0 - ((dow0 - wkst + 7) % 7) * ICS_DAY + p * interval * 7 * ICS_DAY;
      const weekdays = rules.byday.length ? rules.byday.map(b => b.d) : [dow0];
      days = weekdays.map(d => periodStart + ((d - wkst + 7) % 7) * ICS_DAY).sort((a, b) => a - b);
    } else if (freq === 'MONTHLY') {
      periodStart = Date.UTC(y0, m0 + p * interval, 1);
      days = icsMonthDays_(periodStart, rules, dom0);
    } else {
      periodStart = Date.UTC(y0 + p * interval, 0, 1);
      const months = bymonth.length ? bymonth.slice().sort((a, b) => a - b) : [m0 + 1];
      days = [];
      months.forEach(m => { days = days.concat(icsMonthDays_(Date.UTC(y0 + p * interval, m - 1, 1), rules, dom0)); });
    }
    if (periodStart > hi || periodStart > until) return;
    if (bymonth.length && freq !== 'YEARLY') days = days.filter(d => bymonth.indexOf(new Date(d).getUTCMonth() + 1) >= 0);
    for (let i = 0; i < days.length; i++) {
      const f = days[i] + tod;
      if (f < st.f) continue;
      if (f > until || f > hi || n >= count) return;
      n++;
      if (f >= lo) cb(f);
    }
  }
}

// Candidate days of one month for MONTHLY and YEARLY rules.
function icsMonthDays_(monthStart, rules, dom0) {
  const first = new Date(monthStart);
  const y = first.getUTCFullYear(), m = first.getUTCMonth();
  const dim = new Date(Date.UTC(y, m + 1, 0)).getUTCDate();
  const resolve = md => (md > 0 ? md : dim + md + 1);
  let days = [];
  if (rules.byday.length) {
    const dow1 = first.getUTCDay();
    rules.byday.forEach(b => {
      const all = [];
      for (let d = 1 + ((b.d - dow1 + 7) % 7); d <= dim; d += 7) all.push(d);
      if (!b.n) {
        days = days.concat(all);
      } else {
        const x = b.n > 0 ? all[b.n - 1] : all[all.length + b.n];
        if (x) days.push(x);
      }
    });
    if (rules.bymonthday.length) days = days.filter(d => rules.bymonthday.some(md => resolve(md) === d));
  } else if (rules.bymonthday.length) {
    days = rules.bymonthday.map(resolve);
  } else {
    days = [dom0];  // e.g. the 31st: months without one are skipped
  }
  days = days.filter((d, i, a) => d >= 1 && d <= dim && a.indexOf(d) === i).sort((a, b) => a - b);
  if (rules.bysetpos.length) {
    const all = days;
    days = rules.bysetpos.map(sp => (sp > 0 ? all[sp - 1] : all[all.length + sp]))
      .filter((d, i, a) => d && a.indexOf(d) === i).sort((a, b) => a - b);
  }
  return days.map(d => Date.UTC(y, m, d));
}

// "NAME;PARAM=x;PARAM="y:z":value" -> { name, params, value }
function icsProp_(line) {
  let quoted = false, i = 0;
  for (; i < line.length; i++) {
    const ch = line[i];
    if (ch === '"') quoted = !quoted;
    else if (ch === ':' && !quoted) break;
  }
  if (!line || i >= line.length) return null;
  const head = line.substring(0, i).split(';');
  const params = {};
  for (let j = 1; j < head.length; j++) {
    const eq = head[j].indexOf('=');
    if (eq > 0) params[head[j].substring(0, eq).toUpperCase()] = head[j].substring(eq + 1).replace(/^"|"$/g, '');
  }
  return { name: head[0].trim().toUpperCase(), params: params, value: line.substring(i + 1) };
}

// DATE or DATE-TIME -> { f: floating ms, tz, allDay }. UTC times get tz 'UTC'.
function icsTime_(p, tz) {
  const m = /^(\d{4})(\d{2})(\d{2})(?:T(\d{2})(\d{2})(\d{2})?(Z)?)?$/i.exec(String(p.value || '').trim());
  if (!m) return null;
  const f = Date.UTC(Number(m[1]), Number(m[2]) - 1, Number(m[3]), Number(m[4] || 0), Number(m[5] || 0), Number(m[6] || 0));
  if (m[4] === undefined) return { f: f, tz: tz, allDay: true };
  if (m[7]) return { f: f, tz: 'UTC', allDay: false };
  return { f: f, tz: icsZone_(p.params && p.params.TZID, tz), allDay: false };
}

function icsZone_(tzid, fallback) {
  if (!tzid) return fallback;
  tzid = String(tzid).trim().replace(/^\/+/, '');
  if (WINDOWS_TZ[tzid]) return WINDOWS_TZ[tzid];
  if (/^(UTC|GMT|Etc\/[\w+\-]+|[A-Za-z]+\/[\w+\-]+(\/[\w+\-]+)?)$/.test(tzid)) return tzid;
  return fallback;  // custom Outlook zones: assume the calendar's own
}

// Floating ms in a time zone -> epoch ms.
function toEpoch_(f, tz) {
  if (tz === 'UTC') return f;
  const off = offsetMs_(f, tz);
  const t = f - off;
  const off2 = offsetMs_(t, tz);  // DST change between the two guesses
  return off2 === off ? t : f - off2;
}

function offsetMs_(t, tz) {
  return utcOffsetMinutes_(new Date(t), tz) * 60000;
}

function icsText_(v) {
  if (!v) return '';
  return String(v).replace(/\\n/gi, '\n').replace(/\\([,;\\])/g, '$1').trim();
}

function icsDuration_(v) {
  const m = /^([+-])?P(?:(\d+)W)?(?:(\d+)D)?(?:T(?:(\d+)H)?(?:(\d+)M)?(?:(\d+)S)?)?$/.exec(String(v).trim());
  if (!m) return 0;
  const secs = (Number(m[2] || 0) * 7 + Number(m[3] || 0)) * 86400 + Number(m[4] || 0) * 3600 +
    Number(m[5] || 0) * 60 + Number(m[6] || 0);
  return (m[1] === '-' ? -1 : 1) * secs * 1000;
}

// ---- Helpers --------------------------------------------------------------

function cached_(key, seconds, fn) {
  const cache = CacheService.getScriptCache();
  const hit = cache.get(key);
  if (hit) return JSON.parse(hit);
  const value = fn();
  try {
    cache.put(key, JSON.stringify(value), seconds);
  } catch (err) {
    // over the cache's 100 KB per value: just not cached
  }
  return value;
}

// Midnight starting this day (yyyy-MM-dd) in the time zone, epoch ms.
function dayMs_(day, tz) {
  return Utilities.parseDate(day, tz, 'yyyy-MM-dd').getTime();
}

// This week's Monday 00:00 in the time zone, epoch ms.
function startOfWeek_(now, tz) {
  const weekday = Number(Utilities.formatDate(now, tz, 'u'));  // 1 = Monday
  const today = Utilities.formatDate(now, tz, 'yyyy-MM-dd').split('-').map(Number);
  const monday = new Date(Date.UTC(today[0], today[1] - 1, today[2] - (weekday - 1)));
  return dayMs_(Utilities.formatDate(monday, 'UTC', 'yyyy-MM-dd'), tz);
}

function hash_(s) {
  return Utilities.base64EncodeWebSafe(
    Utilities.computeDigest(Utilities.DigestAlgorithm.MD5, s, Utilities.Charset.UTF_8)).substring(0, 22);
}

function utcOffsetMinutes_(date, tz) {
  const z = Utilities.formatDate(date, tz, 'Z');  // e.g. "+0200"
  const sign = z[0] === '-' ? -1 : 1;
  return sign * (Number(z.substr(1, 2)) * 60 + Number(z.substr(3, 2)));
}

function plainText_(s, max) {
  if (!s) return '';
  const text = String(s)
    .replace(/<br\s*\/?>/gi, '\n').replace(/<[^>]+>/g, '')
    .replace(/&nbsp;/g, ' ').replace(/&amp;/g, '&').replace(/&lt;/g, '<').replace(/&gt;/g, '>')
    .replace(/\n{3,}/g, '\n\n').trim();
  return text.length > max ? text.substring(0, max - 1) + '…' : text;
}

function withLock_(fn) {
  const lock = LockService.getScriptLock();
  lock.waitLock(10000);
  try {
    fn();
  } finally {
    lock.releaseLock();
  }
}

function json_(obj) {
  return ContentService.createTextOutput(JSON.stringify(obj))
    .setMimeType(ContentService.MimeType.JSON);
}

/** Run this once from the editor to grant permissions and preview the output. */
function preview() {
  Logger.log(JSON.stringify(feed_({}, []), null, 2));
}

/** Run from the editor to bring back everything you dismissed on the display. */
function clearDismissed() {
  PropertiesService.getScriptProperties().deleteProperty(DISMISSED_KEY);
}
