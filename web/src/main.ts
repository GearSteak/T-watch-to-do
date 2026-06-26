import { BleClient } from "./ble/client";
import { WATCHFACE_IMAGE_W } from "./api/types";
import type { TodoItem, TodoPayload, WatchfaceConfig } from "./api/types";

const client = new BleClient();

const btnConnect = document.getElementById("btn-connect") as HTMLButtonElement;
const deviceStatus = document.getElementById("device-status")!;
const timeSection = document.getElementById("time-section")!;
const todoSection = document.getElementById("todo-section")!;
const watchfaceSection = document.getElementById("watchface-section")!;
const browserTime = document.getElementById("browser-time")!;
const btnSetTime = document.getElementById("btn-set-time") as HTMLButtonElement;
const todoForm = document.getElementById("todo-form") as HTMLFormElement;
const todoInput = document.getElementById("todo-input") as HTMLInputElement;
const todoActive = document.getElementById("todo-active")!;
const todoCompleted = document.getElementById("todo-completed")!;
const btnRefreshTodos = document.getElementById("btn-refresh-todos") as HTMLButtonElement;
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

function normalizeTodo(item: TodoItem): TodoItem {
  return {
    ...item,
    priority: (item.priority ?? 0) as TodoItem["priority"],
    sortOrder: item.sortOrder ?? 0,
  };
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
    }

    const cb = document.createElement("input");
    cb.type = "checkbox";
    cb.checked = item.done;
    cb.addEventListener("change", () => toggleTodo(item.id));

    const span = document.createElement("span");
    span.className = "todo-text";
    span.textContent = item.text;

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

async function syncTodosToWatch() {
  await client.writeTodos({ items: todos });
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
  todos.push({
    id,
    text,
    done: false,
    priority: 0,
    sortOrder: nextSortOrder(todos),
    createdAt: Date.now(),
    completedAt: 0,
  });
  todoInput.value = "";
  renderTodos();
  await syncTodosToWatch();
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
