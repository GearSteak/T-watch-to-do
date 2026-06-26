import {
  SVC_UUID,
  CHR_DEVICE_INFO,
  CHR_TIME_SYNC,
  CHR_TODO_SYNC,
  CHR_BATTERY_ALERT,
  CHR_COMPLETED_LOG,
  CHR_WATCHFACE_META,
  CHR_WATCHFACE_IMAGE,
  WF_IMG_OP_BEGIN,
  WF_IMG_OP_DATA,
  WF_IMG_OP_COMMIT,
  WF_IMG_OP_CLEAR,
  TODO_OP_UPLOAD_BEGIN,
  TODO_OP_UPLOAD_DATA,
  TODO_OP_UPLOAD_COMMIT,
  TODO_OP_DL_PREPARE,
  TODO_OP_DL_PAGE,
  TODO_PING_CHANGED,
  TODO_PAGE_SIZE,
  type DeviceInfo,
  type TodoPayload,
  type BatteryAlert,
  type WatchfaceConfig,
} from "../api/types";

function u8(...bytes: number[]): ArrayBuffer {
  return new Uint8Array(bytes).buffer;
}

function decodeJson<T>(value: DataView): T {
  const decoder = new TextDecoder();
  const text = decoder.decode(value);
  return JSON.parse(text) as T;
}

function encodeJson(obj: unknown): ArrayBuffer {
  const encoded = new TextEncoder().encode(JSON.stringify(obj));
  return encoded.buffer.slice(
    encoded.byteOffset,
    encoded.byteOffset + encoded.byteLength,
  ) as ArrayBuffer;
}

export class BleClient {
  private device: BluetoothDevice | null = null;
  private server: BluetoothRemoteGATTServer | null = null;
  private chrDeviceInfo: BluetoothRemoteGATTCharacteristic | null = null;
  private chrTimeSync: BluetoothRemoteGATTCharacteristic | null = null;
  private chrTodoSync: BluetoothRemoteGATTCharacteristic | null = null;
  private chrBatteryAlert: BluetoothRemoteGATTCharacteristic | null = null;
  private chrCompletedLog: BluetoothRemoteGATTCharacteristic | null = null;
  private chrWatchfaceMeta: BluetoothRemoteGATTCharacteristic | null = null;
  private chrWatchfaceImage: BluetoothRemoteGATTCharacteristic | null = null;
  private disconnectCb: (() => void) | null = null;
  private todoCb: ((payload: TodoPayload) => void) | null = null;
  private opChain: Promise<unknown> = Promise.resolve();

  get connected(): boolean {
    return !!this.server?.connected;
  }

  // Serializes all GATT operations so multi-step transfers (paged todo
  // download, chunked uploads) never overlap and trigger
  // "GATT operation already in progress".
  private run<T>(fn: () => Promise<T>): Promise<T> {
    const next = this.opChain.then(fn, fn);
    this.opChain = next.catch(() => undefined);
    return next as Promise<T>;
  }

  onDisconnect(cb: () => void): void {
    this.disconnectCb = cb;
  }

  async connect(): Promise<void> {
    if (!navigator.bluetooth) {
      if (!window.isSecureContext) {
        throw new Error(
          "Bluetooth needs a secure page. Open this site over https:// or via localhost (a plain http://<ip> address won't work).",
        );
      }
      throw new Error(
        "Web Bluetooth isn't available in this browser. Use Chrome or Edge on Windows/Android (it does not work on iPhone/iPad).",
      );
    }

    this.device = await navigator.bluetooth.requestDevice({
      filters: [{ services: [SVC_UUID] }],
      optionalServices: [SVC_UUID],
    });

    this.device.addEventListener("gattserverdisconnected", () => {
      this.server = null;
      this.disconnectCb?.();
    });

    this.server = await this.device.gatt!.connect();
    const service = await this.server.getPrimaryService(SVC_UUID);

    this.chrDeviceInfo = await service.getCharacteristic(CHR_DEVICE_INFO);
    this.chrTimeSync = await service.getCharacteristic(CHR_TIME_SYNC);
    this.chrTodoSync = await service.getCharacteristic(CHR_TODO_SYNC);
    this.chrBatteryAlert = await service.getCharacteristic(CHR_BATTERY_ALERT);
    this.chrCompletedLog = await service.getCharacteristic(CHR_COMPLETED_LOG);
    this.chrWatchfaceMeta = await service.getCharacteristic(CHR_WATCHFACE_META);
    this.chrWatchfaceImage = await service.getCharacteristic(CHR_WATCHFACE_IMAGE);

    await this.chrDeviceInfo.startNotifications();
    await this.chrTodoSync.startNotifications();
    await this.chrBatteryAlert.startNotifications();
    await this.chrCompletedLog.startNotifications();
    await this.chrWatchfaceMeta.startNotifications();
  }

  onDeviceInfo(cb: (info: DeviceInfo) => void): void {
    this.chrDeviceInfo?.addEventListener("characteristicvaluechanged", (e) => {
      const target = e.target as BluetoothRemoteGATTCharacteristic;
      if (target.value) cb(decodeJson<DeviceInfo>(target.value));
    });
  }

  onTodoSync(cb: (payload: TodoPayload) => void): void {
    this.todoCb = cb;
    this.chrTodoSync?.addEventListener("characteristicvaluechanged", (e) => {
      const target = e.target as BluetoothRemoteGATTCharacteristic;
      const v = target.value;
      if (!v || v.byteLength === 0) return;
      // The watch sends a 1-byte "changed" ping; pull the full list via the
      // paged download protocol in response.
      if (v.getUint8(0) === TODO_PING_CHANGED) {
        this.readTodos()
          .then((payload) => this.todoCb?.(payload))
          .catch(() => undefined);
      }
    });
  }

  onBatteryAlert(cb: (alert: BatteryAlert) => void): void {
    this.chrBatteryAlert?.addEventListener("characteristicvaluechanged", (e) => {
      const target = e.target as BluetoothRemoteGATTCharacteristic;
      if (target.value) cb(decodeJson<BatteryAlert>(target.value));
    });
  }

  onWatchfaceSync(cb: (config: WatchfaceConfig) => void): void {
    this.chrWatchfaceMeta?.addEventListener("characteristicvaluechanged", (e) => {
      const target = e.target as BluetoothRemoteGATTCharacteristic;
      if (!target.value) return;
      try {
        cb(decodeJson<WatchfaceConfig>(target.value));
      } catch {
        /* ignore truncated/partial notification */
      }
    });
  }

  async readDeviceInfo(): Promise<DeviceInfo> {
    const value = await this.chrDeviceInfo!.readValue();
    return decodeJson<DeviceInfo>(value);
  }

  async readTodos(): Promise<TodoPayload> {
    return this.run(() => this.readTodosInner());
  }

  private async readTodosInner(): Promise<TodoPayload> {
    const chr = this.chrTodoSync!;
    await chr.writeValueWithResponse(u8(TODO_OP_DL_PREPARE));

    const header = await chr.readValue();
    const total = header.byteLength >= 4 ? header.getUint32(0, true) : 0;
    if (total === 0) return { items: [] };

    const buf = new Uint8Array(total);
    let received = 0;
    while (received < total) {
      const req = new Uint8Array(5);
      req[0] = TODO_OP_DL_PAGE;
      new DataView(req.buffer).setUint32(1, received, true);
      await chr.writeValueWithResponse(req.buffer as ArrayBuffer);

      const page = await chr.readValue();
      if (page.byteLength === 0) break;
      const pageBytes = new Uint8Array(page.buffer, page.byteOffset, page.byteLength);
      const take = Math.min(pageBytes.length, total - received);
      buf.set(pageBytes.subarray(0, take), received);
      received += take;
      if (pageBytes.length < TODO_PAGE_SIZE && received < total) {
        // Short page before reaching total: nothing more is coming.
        break;
      }
    }

    const text = new TextDecoder().decode(buf.subarray(0, received));
    return JSON.parse(text) as TodoPayload;
  }

  async writeTodos(payload: TodoPayload): Promise<void> {
    return this.run(() => this.writeTodosInner(payload));
  }

  private async writeTodosInner(payload: TodoPayload): Promise<void> {
    const chr = this.chrTodoSync!;
    const data = new TextEncoder().encode(JSON.stringify(payload));

    const begin = new Uint8Array(5);
    begin[0] = TODO_OP_UPLOAD_BEGIN;
    new DataView(begin.buffer).setUint32(1, data.length, true);
    await chr.writeValueWithResponse(begin.buffer as ArrayBuffer);

    const CHUNK = 180;
    for (let offset = 0; offset < data.length; offset += CHUNK) {
      const slice = data.subarray(offset, Math.min(offset + CHUNK, data.length));
      const packet = new Uint8Array(5 + slice.length);
      packet[0] = TODO_OP_UPLOAD_DATA;
      new DataView(packet.buffer).setUint32(1, offset, true);
      packet.set(slice, 5);
      await chr.writeValueWithResponse(packet.buffer as ArrayBuffer);
    }

    await chr.writeValueWithResponse(u8(TODO_OP_UPLOAD_COMMIT));
  }

  async setTime(unixMs: number, tzOffsetMinutes: number): Promise<void> {
    const buf = new ArrayBuffer(12);
    const view = new DataView(buf);
    view.setBigInt64(0, BigInt(unixMs), true);
    view.setInt32(8, tzOffsetMinutes, true);
    await this.chrTimeSync!.writeValue(buf);
  }

  async readWatchface(): Promise<WatchfaceConfig> {
    const value = await this.chrWatchfaceMeta!.readValue();
    return decodeJson<WatchfaceConfig>(value);
  }

  async writeWatchface(config: WatchfaceConfig): Promise<void> {
    await this.chrWatchfaceMeta!.writeValue(encodeJson(config));
  }

  // Streams a 240x240 RGB565 (little-endian) image to the watch in chunks.
  async uploadWatchfaceImage(
    rgb565: Uint8Array,
    onProgress?: (sent: number, total: number) => void,
  ): Promise<void> {
    const chr = this.chrWatchfaceImage!;
    const total = rgb565.length;

    const begin = new Uint8Array(5);
    begin[0] = WF_IMG_OP_BEGIN;
    new DataView(begin.buffer).setUint32(1, total, true);
    await chr.writeValueWithResponse(begin.buffer as ArrayBuffer);

    const CHUNK = 180;
    for (let offset = 0; offset < total; offset += CHUNK) {
      const slice = rgb565.subarray(offset, Math.min(offset + CHUNK, total));
      const packet = new Uint8Array(5 + slice.length);
      packet[0] = WF_IMG_OP_DATA;
      new DataView(packet.buffer).setUint32(1, offset, true);
      packet.set(slice, 5);
      await chr.writeValueWithResponse(packet.buffer as ArrayBuffer);
      onProgress?.(Math.min(offset + CHUNK, total), total);
    }

    const commit = new Uint8Array([WF_IMG_OP_COMMIT]);
    await chr.writeValueWithResponse(commit.buffer as ArrayBuffer);
  }

  async clearWatchfaceImage(): Promise<void> {
    const clear = new Uint8Array([WF_IMG_OP_CLEAR]);
    await this.chrWatchfaceImage!.writeValueWithResponse(clear.buffer as ArrayBuffer);
  }
}
