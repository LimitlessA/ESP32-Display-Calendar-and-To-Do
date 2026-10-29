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
 *   action=complete&list=L&task=ID     mark a task done
 *   action=uncomplete&list=L&task=ID   undo that
 *   action=dismiss&event=KEY&until=MS  hide one event occurrence on the display
 *   action=showcal&id=CAL&shown=0|1    choose which calendars the display shows
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
};

const DISMISSED_KEY = 'dismissed';
const HIDDEN_CALS_KEY = 'hiddenCalendars';

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
    events: getEvents_(now, new Date(now.getTime() + CONFIG.DAYS_AHEAD * 86400000), CONFIG.MAX_EVENTS),
    tasks: getTasks_(),
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

function getTasks_() {
  const lists = (Tasks.Tasklists.list({ maxResults: 100 }).items || [])
    .filter(l => !CONFIG.TASKLIST_IDS.length || CONFIG.TASKLIST_IDS.includes(l.id));

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

// ---- Helpers --------------------------------------------------------------

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
