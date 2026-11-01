/**
 * Calendar + Google Tasks feed for the ESP32 display.
 *
 * Deploy one copy of this script in every Google account you want on the
 * display, as a web app (Execute as: Me, Who has access: Anyone), then add its
 * URL and token on the display's setup page (http://calendar.local).
 *
 * All calls are GET <web app URL>?token=T[&action=...]:
 *   (no action)                        feed: upcoming events, open tasks, calendars
 *   action=range&from=MS&to=MS         events in a time range (week view)
 *   action=complete&list=L&task=ID     mark a task done (a task deleted elsewhere counts as done)
 *   action=uncomplete&list=L&task=ID   undo that
 *   action=dismiss&event=KEY&until=MS  hide one event occurrence on the display
 *   action=showcal&id=CAL&shown=0|1    choose which calendars the display shows
 *   action=add&title=T[&list=L][&due=D]  add a task (D = yyyy-MM-dd; no list = default list)
 *   action=due&list=L&task=ID&due=D    move a task to another day (empty D = no date)
 *   action=star&event=KEY&on=1|0&title=T&start=MS&allDay=0|1
 *                                      star an event so the display counts down to it
 *
 * Times are epoch milliseconds; the display formats them itself. Non-Google
 * calendars (Outlook, iCloud, Exchange) show up once you add their ICS link in
 * Google Calendar under "Other calendars > From URL".
 */

const CONFIG = {
  TOKEN: 'change-me',  // paste the same value on the display's setup page
  DAYS_AHEAD: 7,
  MAX_EVENTS: 60,
  MAX_RANGE_EVENTS: 120,
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

function doGet(e) {
  const p = (e && e.parameter) || {};
  if (p.token !== CONFIG.TOKEN) {
    return json_({ error: 'unauthorized' });
  }
  try {
    switch (p.action || '') {
      case '': return json_(feed_());
      case 'range': return json_(range_(Number(p.from), Number(p.to)));
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

function feed_() {
  const tz = CalendarApp.getDefaultCalendar().getTimeZone();
  const now = new Date();
  const hidden = loadHiddenCalendars_();
  const all = CalendarApp.getAllCalendars().filter(c => !c.isHidden());
  return {
    account: Session.getEffectiveUser().getEmail(),
    tz: tz,
    utcOffsetMin: utcOffsetMinutes_(now, tz),
    calendars: all.map(c => ({
      id: c.getId(), name: c.getName(), color: c.getColor(), shown: !hidden[c.getId()],
    })),
    events: addTravel_(getEvents_(now, new Date(now.getTime() + CONFIG.DAYS_AHEAD * 86400000), CONFIG.MAX_EVENTS)),
    tasks: getTasks_(),
    lists: taskLists_().map(l => ({ id: l.id, title: l.title })),
    dates: getDates_(tz),
    frequent: frequentTasks_(),
  };
}

function range_(from, to) {
  if (!(from > 0) || !(to > from) || to - from > 45 * 86400000) throw new Error('bad range');
  return { events: getEvents_(new Date(from), new Date(to), CONFIG.MAX_RANGE_EVENTS) };
}

// ---- Calendar -------------------------------------------------------------

// Recurring events share one ID, so an occurrence is identified by ID + start.
function eventKey_(ev) {
  return ev.getId() + '|' + ev.getStartTime().getTime();
}

function shownCalendars_() {
  const hidden = loadHiddenCalendars_();
  return CalendarApp.getAllCalendars().filter(c => !c.isHidden() && !hidden[c.getId()]);
}

function getEvents_(from, to, max) {
  const dismissed = loadDismissed_();
  const events = [];
  const seen = {};
  shownCalendars_().forEach(cal => {
    cal.getEvents(from, to).forEach(ev => {
      if (ev.getMyStatus() === CalendarApp.GuestStatus.NO) return;  // declined
      const key = eventKey_(ev);
      if (dismissed[key] || seen[key]) return;  // same event can sit in two calendars
      seen[key] = true;
      events.push({ ev: ev, cal: cal, key: key });
    });
  });

  events.sort((a, b) => a.ev.getStartTime() - b.ev.getStartTime()
    || Number(b.ev.isAllDayEvent()) - Number(a.ev.isAllDayEvent()));

  return events.slice(0, max).map(({ ev, cal, key }) => ({
    key: key,
    start: ev.getStartTime().getTime(),
    end: ev.getEndTime().getTime(),
    allDay: ev.isAllDayEvent(),
    title: ev.getTitle() || '(no title)',
    cal: cal.getName(),
    color: cal.getColor(),
    loc: ev.getLocation() || '',
    desc: plainText_(ev.getDescription(), 280),
    org: (ev.getCreators()[0] || ''),
    guests: ev.getGuestList().length,
  }));
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

// The list that holds the task: the one the display sent if it is right,
// otherwise the first list that has it (no list sent, or the task was moved
// to another list elsewhere). null if the task is gone.
function findTaskList_(listId, taskId) {
  const ids = [listId].concat(taskLists_().map(l => l.id))
    .filter((id, i, all) => id && all.indexOf(id) === i);
  for (const id of ids) {
    try {
      const t = Tasks.Tasks.get(id, taskId);
      if (t && !t.deleted) return id;
    } catch (err) {
      // not in this list
    }
  }
  return null;
}

function setTaskStatus_(listId, taskId, done) {
  if (!taskId) throw new Error('missing task');
  const list = findTaskList_(listId, taskId);
  if (!list) {
    if (done) return { ok: true, gone: true };  // already deleted elsewhere: nothing left to do
    throw new Error('task not found');
  }
  Tasks.Tasks.patch(done ? { status: 'completed' } : { status: 'needsAction', completed: null },
    list, taskId);
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
  if (!taskId) throw new Error('missing task');
  const list = findTaskList_(listId, taskId);
  if (!list) throw new Error('task not found');
  Tasks.Tasks.patch({ due: dueValue_(due) }, list, taskId);
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

// ---- Helpers --------------------------------------------------------------

function cached_(key, seconds, fn) {
  const cache = CacheService.getScriptCache();
  const hit = cache.get(key);
  if (hit) return JSON.parse(hit);
  const value = fn();
  cache.put(key, JSON.stringify(value), seconds);
  return value;
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
  Logger.log(JSON.stringify(feed_(), null, 2));
}

/** Run from the editor to bring back everything you dismissed on the display. */
function clearDismissed() {
  PropertiesService.getScriptProperties().deleteProperty(DISMISSED_KEY);
}
