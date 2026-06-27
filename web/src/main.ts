import { BleClient } from "./ble/client";
import { WATCHFACE_IMAGE_W } from "./api/types";
import type { TodoItem, TodoPayload, WatchfaceConfig, AlarmItem, AlarmPayload } from "./api/types";

const client = new BleClient();

const btnConnect = document.getElementById("btn-connect") as HTMLButtonElement;
const deviceStatus = document.getElementById("device-status")!;
const timeSection = document.getElementById("time-section")!;
const todoSection = document.getElementById("todo-section")!;
const alarmSection = document.getElementById("alarm-section")!;
const watchfaceSection = document.getElementById("watchface-section")!;
const browserTime = document.getElementById("browser-time")!;
const btnSetTime = document.getElementById("btn-set-time") as HTMLButtonElement;
const todoForm = document.getElementById("todo-form") as HTMLFormElement;
const todoInput = document.getElementById("todo-input") as HTMLInputElement;
const todoRepeat = document.getElementById("todo-repeat") as HTMLSelectElement;
const todoRepeatWeekday = document.getElementById("todo-repeat-weekday") as HTMLSelectElement;
const todoRepeatDays = document.getElementById("todo-repeat-days") as HTMLInputElement;
const todoRepeatMonthday = document.getElementById("todo-repeat-monthday") as HTMLSelectElement;
const todoRemind = document.getElementById("todo-remind") as HTMLInputElement;
const repeatWeeklyWrap = document.getElementById("repeat-weekly-wrap")!;
const repeatIntervalWrap = document.getElementById("repeat-interval-wrap")!;
const repeatMonthlyWrap = document.getElementById("repeat-monthly-wrap")!;
const todoActive = document.getElementById("todo-active")!;
const todoCompleted = document.getElementById("todo-completed")!;
const btnRefreshTodos = document.getElementById("btn-refresh-todos") as HTMLButtonElement;
const alarmForm = document.getElementById("alarm-form") as HTMLFormElement;
const alarmTime = document.getElementById("alarm-time") as HTMLInputElement;
const alarmLabel = document.getElementById("alarm-label") as HTMLInputElement;
const alarmList = document.getElementById("alarm-list")!;
const toast = document.getElementById("toast")!;
const faceCanvas = document.getElementById("face-preview") as HTMLCanvasElement;
const facePreset = document.getElementById("face-preset") as HTMLSelectElement;
const faceShowDate = document.getElementById("face-show-date") as HTMLInputElement;
const faceShowBattery = document.getElementById("face-show-battery") as HTMLInputElement;
const faceBg = document.getElementById("face-bg") as HTMLInputElement;
const faceTimeColor = document.getElementById("face-time-color") as HTMLInputElement;
const faceDateColor = document.getElementById("face-date-color") as HTMLInputElement;
const btnSyncFace = document.getElementById("btn-sync-face") as HTMLButtonElement;
const faceImageInput = document.getElementById("face-image-input") as HTMLInputElement;
const btnUploadImage = document.getElementById("btn-upload-image") as HTMLButtonElement;
const btnClearImage = document.getElementById("btn-clear-image") as HTMLButtonElement;
const uploadProgress = document.getElementById("upload-progress")!;
const uploadProgressBar = document.getElementById("upload-progress-bar")!;

let todos: TodoItem[] = [];
let alarms: AlarmItem[] = [];
const faceCtx = faceCanvas.getContext("2d")!;
let faceImage: HTMLImageElement | null = null;

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

// GATT reads/writes can transiently fail right after connecting (the watch
// pushes notifications the moment the link opens, which collide with reads).
// Retry a few times before surfacing the error.
async function withRetry<T>(fn: () => Promise<T>, attempts = 5, delayMs = 250): Promise<T> {
  let lastErr: unknown;
  for (let i = 0; i < attempts; i += 1) {
    try {
      return await fn();
    } catch (err) {
      lastErr = err;
      if (!client.connected) break;
      await sleep(delayMs);
    }
  }
  throw lastErr;
}

function errMessage(err: unknown): string {
  return err instanceof Error ? err.message : String(err);
}

const PRIORITY_LABELS = ["None", "Low", "Medium", "High"] as const;
const WEEKDAY_LABELS = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"] as const;
const NO_REMIND = 65535;

function minuteToHHMM(min: number | undefined): string {
  if (min === undefined || min >= NO_REMIND || min < 0) return "";
  const h = Math.floor(min / 60);
  const m = min % 60;
  return `${String(h).padStart(2, "0")}:${String(m).padStart(2, "0")}`;
}

function hhmmToMinute(value: string): number {
  if (!value) return NO_REMIND;
  const [h, m] = value.split(":").map(Number);
  if (Number.isNaN(h) || Number.isNaN(m)) return NO_REMIND;
  return h * 60 + m;
}

function formatRemind(min: number | undefined): string {
  const hhmm = minuteToHHMM(min);
  if (!hhmm) return "";
  const d = new Date();
  const [h, m] = hhmm.split(":").map(Number);
  d.setHours(h, m, 0, 0);
  return d.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
}
const REPEAT_OPTIONS: { value: TodoItem["repeat"]; label: string }[] = [
  { value: 0, label: "Once" },
  { value: 1, label: "Daily" },
  { value: 2, label: "Weekly" },
  { value: 3, label: "Every N days" },
  { value: 4, label: "Monthly" },
];

type RepeatFields = Pick<TodoItem, "repeat" | "repeatWeekday" | "repeatIntervalDays" | "repeatMonthDay">;

function fillMonthDaySelect(select: HTMLSelectElement, selected = 1) {
  select.innerHTML = "";
  for (let day = 1; day <= 31; day += 1) {
    const opt = document.createElement("option");
    opt.value = String(day);
    opt.textContent = day === 31 ? "31 (or last day)" : String(day);
    select.appendChild(opt);
  }
  select.value = String(Math.max(1, Math.min(31, selected)));
}

fillMonthDaySelect(todoRepeatMonthday, 1);

function applyRepeatMode(fields: RepeatFields, mode: TodoItem["repeat"], overrides?: Partial<RepeatFields>): RepeatFields {
  const next: RepeatFields = {
    repeat: mode,
    repeatWeekday: 0,
    repeatIntervalDays: 0,
    repeatMonthDay: 0,
  };
  if (mode === 2) {
    next.repeatWeekday = overrides?.repeatWeekday ?? fields.repeatWeekday ?? 1;
  } else if (mode === 3) {
    next.repeatIntervalDays = Math.max(2, overrides?.repeatIntervalDays ?? fields.repeatIntervalDays ?? 3);
  } else if (mode === 4) {
    next.repeatMonthDay = Math.max(1, Math.min(31, overrides?.repeatMonthDay ?? fields.repeatMonthDay ?? 1));
  }
  return next;
}

function createRepeatModeSelect(value: TodoItem["repeat"], className = "todo-repeat"): HTMLSelectElement {
  const select = document.createElement("select");
  select.className = className;
  for (const opt of REPEAT_OPTIONS) {
    const el = document.createElement("option");
    el.value = String(opt.value);
    el.textContent = opt.label;
    select.appendChild(el);
  }
  select.value = String(value);
  return select;
}

function createWeekdaySelect(value: number): HTMLSelectElement {
  const select = document.createElement("select");
  select.className = "todo-repeat-sub todo-repeat-weekday";
  for (let d = 0; d < WEEKDAY_LABELS.length; d += 1) {
    const opt = document.createElement("option");
    opt.value = String(d);
    opt.textContent = WEEKDAY_LABELS[d];
    select.appendChild(opt);
  }
  select.value = String(value);
  return select;
}

function createIntervalInput(value: number): HTMLInputElement {
  const input = document.createElement("input");
  input.type = "number";
  input.min = "2";
  input.max = "365";
  input.value = String(Math.max(2, value || 3));
  input.className = "todo-repeat-sub todo-repeat-interval";
  return input;
}

function createMonthDaySelect(value: number): HTMLSelectElement {
  const select = document.createElement("select");
  select.className = "todo-repeat-sub todo-repeat-monthday";
  fillMonthDaySelect(select, value || 1);
  return select;
}

function updateRepeatSubVisibility(
  mode: number,
  weekdayEl: HTMLElement,
  intervalEl: HTMLElement,
  monthDayEl: HTMLElement,
) {
  weekdayEl.hidden = mode !== 2;
  intervalEl.hidden = mode !== 3;
  monthDayEl.hidden = mode !== 4;
}

function normalizeTodo(item: TodoItem): TodoItem {
  return {
    ...item,
    priority: (item.priority ?? 0) as TodoItem["priority"],
    repeat: (item.repeat ?? 0) as TodoItem["repeat"],
    repeatWeekday: item.repeatWeekday ?? 0,
    repeatIntervalDays: item.repeatIntervalDays ?? 0,
    repeatMonthDay: item.repeatMonthDay ?? 0,
    remindMinute: item.remindMinute ?? NO_REMIND,
    sortOrder: item.sortOrder ?? 0,
  };
}

function repeatPrefix(item: TodoItem): string {
  let prefix = "";
  if (item.repeat === 1) prefix = "↻ daily ";
  else if (item.repeat === 2) prefix = `↻ ${WEEKDAY_LABELS[item.repeatWeekday ?? 0]} `;
  else if (item.repeat === 3) prefix = `↻ every ${item.repeatIntervalDays || 3}d `;
  else if (item.repeat === 4) prefix = `↻ day ${item.repeatMonthDay || 1} `;
  const remind = formatRemind(item.remindMinute);
  if (remind) prefix += `⏰ ${remind} `;
  return prefix;
}

function updateRepeatFormVisibility() {
  const mode = Number(todoRepeat.value);
  repeatWeeklyWrap.hidden = mode !== 2;
  repeatIntervalWrap.hidden = mode !== 3;
  repeatMonthlyWrap.hidden = mode !== 4;
}

function readRepeatFromForm(): RepeatFields {
  const repeat = Number(todoRepeat.value) as TodoItem["repeat"];
  if (repeat === 2) {
    return { repeat, repeatWeekday: Number(todoRepeatWeekday.value), repeatIntervalDays: 0, repeatMonthDay: 0 };
  }
  if (repeat === 3) {
    const days = Math.max(2, Math.min(365, Number(todoRepeatDays.value) || 3));
    return { repeat, repeatWeekday: 0, repeatIntervalDays: days, repeatMonthDay: 0 };
  }
  if (repeat === 4) {
    return {
      repeat,
      repeatWeekday: 0,
      repeatIntervalDays: 0,
      repeatMonthDay: Number(todoRepeatMonthday.value) || 1,
    };
  }
  return { repeat, repeatWeekday: 0, repeatIntervalDays: 0, repeatMonthDay: 0 };
}

todoRepeat.addEventListener("change", updateRepeatFormVisibility);
updateRepeatFormVisibility();

function formatAlarmTime(hour: number, minute: number): string {
  const d = new Date();
  d.setHours(hour, minute, 0, 0);
  return d.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
}

function sortTodos(items: TodoItem[]): TodoItem[] {
  return [...items].sort((a, b) => {
    if (a.done !== b.done) return Number(a.done) - Number(b.done);
    if (a.priority !== b.priority) return b.priority - a.priority;
    if (a.sortOrder !== b.sortOrder) return a.sortOrder - b.sortOrder;
    return a.createdAt - b.createdAt;
  });
}

function nextSortOrder(items: TodoItem[]): number {
  return items.reduce((max, item) => Math.max(max, item.sortOrder), -1) + 1;
}

function priorityClass(priority: TodoItem["priority"]): string {
  if (priority === 3) return "priority-high";
  if (priority === 2) return "priority-medium";
  if (priority === 1) return "priority-low";
  return "priority-none";
}

function showToast(msg: string, durationMs = 4000) {
  toast.textContent = msg;
  toast.hidden = false;
  setTimeout(() => { toast.hidden = true; }, durationMs);
}

function batteryClass(pct: number): string {
  if (pct <= 10) return "battery-low";
  if (pct <= 20) return "battery-warn";
  return "battery-ok";
}

function updateBrowserTime() {
  browserTime.textContent = new Date().toLocaleString();
}

function renderTodos() {
  todoActive.innerHTML = "";
  todoCompleted.innerHTML = "";

  const sorted = sortTodos(todos);
  const activeSorted = sorted.filter((item) => !item.done);

  for (const item of sorted) {
    const li = document.createElement("li");
    li.className = priorityClass(item.priority);

    if (!item.done) {
      const move = document.createElement("div");
      move.className = "todo-move";

      const btnUp = document.createElement("button");
      btnUp.type = "button";
      btnUp.className = "todo-move-btn";
      btnUp.textContent = "↑";
      btnUp.title = "Move up";
      btnUp.disabled = activeSorted.findIndex((t) => t.id === item.id) === 0;
      btnUp.addEventListener("click", () => moveTodo(item.id, -1));

      const btnDown = document.createElement("button");
      btnDown.type = "button";
      btnDown.className = "todo-move-btn";
      btnDown.textContent = "↓";
      btnDown.title = "Move down";
      btnDown.disabled =
        activeSorted.findIndex((t) => t.id === item.id) === activeSorted.length - 1;
      btnDown.addEventListener("click", () => moveTodo(item.id, 1));

      move.append(btnUp, btnDown);
      li.appendChild(move);

      const priority = document.createElement("select");
      priority.className = "todo-priority";
      for (let level = 0; level < PRIORITY_LABELS.length; level += 1) {
        const opt = document.createElement("option");
        opt.value = String(level);
        opt.textContent = PRIORITY_LABELS[level];
        priority.appendChild(opt);
      }
      priority.value = String(item.priority);
      priority.addEventListener("change", () =>
        setTodoPriority(item.id, Number(priority.value) as TodoItem["priority"])
      );
      li.appendChild(priority);

      const repeatControls = document.createElement("div");
      repeatControls.className = "todo-repeat-controls";

      const repeatSelect = createRepeatModeSelect((item.repeat ?? 0) as TodoItem["repeat"]);
      const weekdaySelect = createWeekdaySelect(item.repeatWeekday ?? 1);
      const intervalInput = createIntervalInput(item.repeatIntervalDays ?? 3);
      const monthDaySelect = createMonthDaySelect(item.repeatMonthDay ?? 1);

      updateRepeatSubVisibility(
        item.repeat ?? 0,
        weekdaySelect,
        intervalInput,
        monthDaySelect,
      );

      const syncRepeat = (overrides?: Partial<RepeatFields>) => {
        const mode = Number(repeatSelect.value) as TodoItem["repeat"];
        void setTodoRepeat(item.id, mode, {
          repeatWeekday: Number(weekdaySelect.value),
          repeatIntervalDays: Number(intervalInput.value),
          repeatMonthDay: Number(monthDaySelect.value),
          ...overrides,
        });
      };

      repeatSelect.addEventListener("change", () => {
        const mode = Number(repeatSelect.value);
        updateRepeatSubVisibility(mode, weekdaySelect, intervalInput, monthDaySelect);
        syncRepeat();
      });
      weekdaySelect.addEventListener("change", () => syncRepeat({ repeatWeekday: Number(weekdaySelect.value) }));
      intervalInput.addEventListener("change", () =>
        syncRepeat({ repeatIntervalDays: Math.max(2, Number(intervalInput.value) || 3) }),
      );
      monthDaySelect.addEventListener("change", () =>
        syncRepeat({ repeatMonthDay: Number(monthDaySelect.value) }),
      );

      const remindInput = document.createElement("input");
      remindInput.type = "time";
      remindInput.className = "todo-repeat-sub todo-remind";
      remindInput.title = "Reminder time";
      remindInput.value = minuteToHHMM(item.remindMinute);
      remindInput.addEventListener("change", () =>
        setTodoReminder(item.id, hhmmToMinute(remindInput.value)),
      );

      repeatControls.append(repeatSelect, weekdaySelect, intervalInput, monthDaySelect, remindInput);
      li.appendChild(repeatControls);
    }

    const cb = document.createElement("input");
    cb.type = "checkbox";
    cb.checked = item.done;
    cb.addEventListener("change", () => toggleTodo(item.id));

    const span = document.createElement("span");
    span.className = "todo-text";
    span.textContent = `${repeatPrefix(item)}${item.text}`;

    li.append(cb, span);
    (item.done ? todoCompleted : todoActive).appendChild(li);
  }
}

function setTodosFromPayload(payload: TodoPayload) {
  todos = payload.items.filter((i) => !i.deleted).map(normalizeTodo);
  renderTodos();
}

async function loadTodosFromWatch(): Promise<boolean> {
  const payload = await withRetry(() => client.readTodos());
  setTodosFromPayload(payload);
  return todos.length > 0;
}

function mergeTodoLists(remote: TodoItem[], local: TodoItem[]): TodoItem[] {
  const byId = new Map<string, TodoItem>();
  for (const item of remote) {
    byId.set(item.id, normalizeTodo(item));
  }
  for (const item of local) {
    const prev = byId.get(item.id);
    byId.set(item.id, normalizeTodo({ ...prev, ...item }));
  }
  return sortTodos([...byId.values()]);
}

async function syncTodosToWatch() {
  const remote = await withRetry(() => client.readTodos());
  todos = mergeTodoLists(remote.items, todos);
  renderTodos();
  await client.writeTodos({ items: todos, fullSync: true });
}

function renderAlarms() {
  alarmList.innerHTML = "";
  const sorted = [...alarms].sort((a, b) => a.hour * 60 + a.minute - (b.hour * 60 + b.minute));

  for (const item of sorted) {
    const li = document.createElement("li");
    li.className = "alarm-item";

    const toggle = document.createElement("input");
    toggle.type = "checkbox";
    toggle.checked = item.enabled;
    toggle.title = "Enabled";
    toggle.addEventListener("change", () => toggleAlarm(item.id, toggle.checked));

    const time = document.createElement("span");
    time.className = "alarm-time";
    time.textContent = formatAlarmTime(item.hour, item.minute);

    const label = document.createElement("span");
    label.className = "alarm-label";
    label.textContent = item.label || "Alarm";

    const del = document.createElement("button");
    del.type = "button";
    del.className = "ghost alarm-delete";
    del.textContent = "Delete";
    del.addEventListener("click", () => deleteAlarm(item.id));

    li.append(toggle, time, label, del);
    alarmList.appendChild(li);
  }
}

function setAlarmsFromPayload(payload: AlarmPayload) {
  alarms = payload.items.filter((i) => !i.deleted);
  renderAlarms();
}

async function syncAlarmsToWatch() {
  await client.writeAlarms({ items: alarms });
}

async function loadAlarmsFromWatch() {
  const payload = await withRetry(() => client.readAlarms());
  setAlarmsFromPayload(payload);
}

async function toggleAlarm(id: string, enabled: boolean) {
  alarms = alarms.map((a) => (a.id === id ? { ...a, enabled } : a));
  renderAlarms();
  await syncAlarmsToWatch();
}

async function deleteAlarm(id: string) {
  alarms = alarms.filter((a) => a.id !== id);
  renderAlarms();
  await syncAlarmsToWatch();
}

function readWatchfaceFromForm(): WatchfaceConfig {
  return {
    preset: facePreset.value as WatchfaceConfig["preset"],
    showDate: faceShowDate.checked,
    showBattery: faceShowBattery.checked,
    bgColor: faceBg.value,
    timeColor: faceTimeColor.value,
    dateColor: faceDateColor.value,
  };
}

function applyWatchfaceToForm(config: WatchfaceConfig) {
  facePreset.value = config.preset;
  faceShowDate.checked = config.showDate;
  faceShowBattery.checked = config.showBattery;
  faceBg.value = config.bgColor;
  faceTimeColor.value = config.timeColor;
  faceDateColor.value = config.dateColor;
  renderFacePreview();
}

function drawImageCover(
  ctx: CanvasRenderingContext2D,
  img: CanvasImageSource,
  iw: number,
  ih: number,
  size: number,
) {
  const scale = Math.max(size / iw, size / ih);
  const dw = iw * scale;
  const dh = ih * scale;
  ctx.drawImage(img, (size - dw) / 2, (size - dh) / 2, dw, dh);
}

function renderFacePreview() {
  const cfg = readWatchfaceFromForm();
  const w = faceCanvas.width;
  const h = faceCanvas.height;

  faceCtx.fillStyle = cfg.bgColor;
  faceCtx.fillRect(0, 0, w, h);

  if (faceImage) {
    drawImageCover(faceCtx, faceImage, faceImage.naturalWidth, faceImage.naturalHeight, w);
  }

  const now = new Date();
  const time = now.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
  const date = now.toLocaleDateString([], { weekday: "short", month: "short", day: "numeric" });

  faceCtx.fillStyle = cfg.timeColor;
  faceCtx.textAlign = "center";
  faceCtx.font = cfg.preset === "bold" ? "bold 42px system-ui" : "32px system-ui";

  if (cfg.preset === "minimal") {
    faceCtx.fillText(time, w / 2, h / 2 + 12);
  } else if (cfg.preset === "bold") {
    faceCtx.fillText(time, w / 2, h / 2 - 4);
    if (cfg.showDate) {
      faceCtx.fillStyle = cfg.dateColor;
      faceCtx.font = "14px system-ui";
      faceCtx.fillText(date, w / 2, h / 2 + 36);
    }
  } else {
    faceCtx.fillText(time, w / 2, h / 2 - 16);
    if (cfg.showDate) {
      faceCtx.fillStyle = cfg.dateColor;
      faceCtx.font = "14px system-ui";
      faceCtx.fillText(date, w / 2, h / 2 + 12);
    }
  }

  if (cfg.showBattery && cfg.preset !== "minimal") {
    faceCtx.fillStyle = cfg.dateColor;
    faceCtx.font = "12px system-ui";
    faceCtx.fillText("85%", w / 2, h - 12);
  }
}

async function toggleTodo(id: string) {
  todos = todos.map((t) =>
    t.id === id
      ? { ...t, done: !t.done, completedAt: !t.done ? Date.now() : 0 }
      : t
  );
  renderTodos();
  await syncTodosToWatch();
}

async function setTodoPriority(id: string, priority: TodoItem["priority"]) {
  todos = todos.map((t) => (t.id === id ? { ...t, priority } : t));
  renderTodos();
  await syncTodosToWatch();
}

async function setTodoRepeat(
  id: string,
  repeat: TodoItem["repeat"],
  extra?: Partial<RepeatFields>,
) {
  todos = todos.map((t) => {
    if (t.id !== id) return t;
    const fields = applyRepeatMode(
      {
        repeat: t.repeat ?? 0,
        repeatWeekday: t.repeatWeekday ?? 0,
        repeatIntervalDays: t.repeatIntervalDays ?? 0,
        repeatMonthDay: t.repeatMonthDay ?? 0,
      },
      repeat,
      extra,
    );
    return { ...t, ...fields };
  });
  renderTodos();
  await syncTodosToWatch();
}

async function setTodoReminder(id: string, remindMinute: number) {
  todos = todos.map((t) => (t.id === id ? { ...t, remindMinute } : t));
  renderTodos();
  await syncTodosToWatch();
}

async function moveTodo(id: string, direction: -1 | 1) {
  const active = sortTodos(todos).filter((t) => !t.done);
  const idx = active.findIndex((t) => t.id === id);
  const swapIdx = idx + direction;
  if (idx < 0 || swapIdx < 0 || swapIdx >= active.length) return;

  const a = todos.find((t) => t.id === active[idx].id);
  const b = todos.find((t) => t.id === active[swapIdx].id);
  if (!a || !b) return;

  const tmp = a.sortOrder;
  a.sortOrder = b.sortOrder;
  b.sortOrder = tmp;

  renderTodos();
  await syncTodosToWatch();
}

function setConnectedUi(connected: boolean) {
  timeSection.hidden = !connected;
  todoSection.hidden = !connected;
  alarmSection.hidden = !connected;
  watchfaceSection.hidden = !connected;
  btnConnect.disabled = connected;
  btnConnect.textContent = connected ? "Connected" : "Connect via Bluetooth";
}

function handleDisconnect() {
  deviceStatus.textContent = "Disconnected";
  setConnectedUi(false);
  showToast("Watch disconnected. Tap Connect to reconnect.");
}

btnConnect.addEventListener("click", async () => {
  btnConnect.disabled = true;
  btnConnect.textContent = "Connecting...";
  try {
    await client.connect();

    client.onDisconnect(handleDisconnect);

    client.onDeviceInfo((info) => {
      const cls = batteryClass(info.battery);
      deviceStatus.innerHTML =
        `Connected &middot; <span class="${cls}">${info.battery}%${info.charging ? " charging" : ""}</span> &middot; fw ${info.fw}`;
    });

    client.onTodoSync(setTodosFromPayload);

    client.onAlarmSync(setAlarmsFromPayload);

    client.onWatchfaceSync(applyWatchfaceToForm);

    client.onBatteryAlert((alert) => {
      showToast(`Watch battery low: ${alert.percent}% (threshold ${alert.level}%)`);
    });

    setConnectedUi(true);
    btnUploadImage.disabled = !faceImage;

    // Let the connection settle so the watch's initial notification burst
    // doesn't collide with our first reads.
    await sleep(300);

    try {
      const info = await withRetry(() => client.readDeviceInfo());
      deviceStatus.innerHTML =
        `Connected &middot; <span class="${batteryClass(info.battery)}">${info.battery}%</span> &middot; fw ${info.fw}`;
    } catch {
      deviceStatus.textContent = "Connected";
    }

    try {
      await loadTodosFromWatch();
    } catch (err) {
      showToast(`Could not read todos: ${errMessage(err)}`);
    }

    try {
      const face = await withRetry(() => client.readWatchface());
      applyWatchfaceToForm(face);
    } catch (err) {
      showToast(`Could not read watch face: ${errMessage(err)}`);
    }

    try {
      await loadAlarmsFromWatch();
    } catch (err) {
      showToast(`Could not read alarms: ${errMessage(err)}`);
    }
  } catch (err) {
    setConnectedUi(false);
    showToast(err instanceof Error ? err.message : "Connection failed");
  }
});

todoForm.addEventListener("submit", async (e) => {
  e.preventDefault();
  const text = todoInput.value.trim();
  if (!text || !client.connected) return;

  const id = crypto.randomUUID().replace(/-/g, "").slice(0, 16);
  const repeatFields = readRepeatFromForm();
  todos.push({
    id,
    text,
    done: false,
    priority: 0,
    ...repeatFields,
    remindMinute: hhmmToMinute(todoRemind.value),
    sortOrder: nextSortOrder(todos),
    createdAt: Date.now(),
    completedAt: 0,
  });
  todoInput.value = "";
  todoRepeat.value = "0";
  todoRemind.value = "";
  updateRepeatFormVisibility();
  renderTodos();
  await syncTodosToWatch();
});

alarmForm.addEventListener("submit", async (e) => {
  e.preventDefault();
  if (!client.connected) return;

  const [hh, mm] = alarmTime.value.split(":").map(Number);
  if (Number.isNaN(hh) || Number.isNaN(mm)) return;
  if (alarms.length >= 8) {
    showToast("Maximum 8 alarms");
    return;
  }

  const id = crypto.randomUUID().replace(/-/g, "").slice(0, 16);
  alarms.push({
    id,
    label: alarmLabel.value.trim() || "Alarm",
    hour: hh,
    minute: mm,
    enabled: true,
  });
  alarmLabel.value = "";
  renderAlarms();
  await syncAlarmsToWatch();
  showToast("Alarm synced to watch", 2000);
});

btnRefreshTodos.addEventListener("click", async () => {
  if (!client.connected) {
    showToast("Not connected to watch");
    return;
  }
  try {
    await loadTodosFromWatch();
    showToast("Todos refreshed from watch", 2000);
  } catch (err) {
    showToast(`Could not read todos: ${errMessage(err)}`);
  }
});

btnSetTime.addEventListener("click", async () => {
  if (!client.connected) return;
  const now = Date.now();
  const tzOffset = -new Date().getTimezoneOffset();
  await client.setTime(now, tzOffset);
  showToast("Watch time updated", 2000);
});

btnSyncFace.addEventListener("click", async () => {
  if (!client.connected) return;
  await client.writeWatchface(readWatchfaceFromForm());
  showToast("Watch face synced", 2000);
});

function imageToRgb565(img: HTMLImageElement, size: number): Uint8Array {
  const canvas = document.createElement("canvas");
  canvas.width = size;
  canvas.height = size;
  const ctx = canvas.getContext("2d")!;
  drawImageCover(ctx, img, img.naturalWidth, img.naturalHeight, size);

  const { data } = ctx.getImageData(0, 0, size, size);
  const out = new Uint8Array(size * size * 2);
  let o = 0;
  for (let i = 0; i < data.length; i += 4) {
    const r = data[i] >> 3;
    const g = data[i + 1] >> 2;
    const b = data[i + 2] >> 3;
    const rgb565 = (r << 11) | (g << 5) | b;
    out[o++] = rgb565 & 0xff;
    out[o++] = (rgb565 >> 8) & 0xff;
  }
  return out;
}

faceImageInput.addEventListener("change", () => {
  const file = faceImageInput.files?.[0];
  if (!file) return;
  const url = URL.createObjectURL(file);
  const img = new Image();
  img.onload = () => {
    faceImage = img;
    btnUploadImage.disabled = !client.connected;
    renderFacePreview();
    URL.revokeObjectURL(url);
  };
  img.onerror = () => {
    showToast("Could not load that image");
    URL.revokeObjectURL(url);
  };
  img.src = url;
});

btnUploadImage.addEventListener("click", async () => {
  if (!client.connected) {
    showToast("Not connected to watch");
    return;
  }
  if (!faceImage) {
    showToast("Pick an image first");
    return;
  }
  const data = imageToRgb565(faceImage, WATCHFACE_IMAGE_W);
  btnUploadImage.disabled = true;
  uploadProgress.hidden = false;
  uploadProgressBar.style.width = "0%";
  try {
    await client.uploadWatchfaceImage(data, (sent, total) => {
      uploadProgressBar.style.width = `${Math.round((sent / total) * 100)}%`;
    });
    showToast("Image uploaded to watch", 2500);
    faceShowDate.dispatchEvent(new Event("change"));
  } catch (err) {
    showToast(`Image upload failed: ${errMessage(err)}`);
  } finally {
    btnUploadImage.disabled = false;
    setTimeout(() => {
      uploadProgress.hidden = true;
    }, 800);
  }
});

btnClearImage.addEventListener("click", async () => {
  faceImage = null;
  faceImageInput.value = "";
  btnUploadImage.disabled = true;
  renderFacePreview();
  if (client.connected) {
    try {
      await client.clearWatchfaceImage();
      showToast("Image removed from watch", 2000);
    } catch (err) {
      showToast(`Could not remove image: ${errMessage(err)}`);
    }
  }
});

for (const el of [facePreset, faceShowDate, faceShowBattery, faceBg, faceTimeColor, faceDateColor]) {
  el.addEventListener("input", renderFacePreview);
  el.addEventListener("change", renderFacePreview);
}

updateBrowserTime();
setInterval(updateBrowserTime, 1000);
setInterval(renderFacePreview, 1000);
renderFacePreview();
