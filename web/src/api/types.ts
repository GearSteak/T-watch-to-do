export const SVC_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
export const CHR_DEVICE_INFO = "6e400010-b5a3-f393-e0a9-e50e24dcca9e";
export const CHR_TIME_SYNC = "6e400011-b5a3-f393-e0a9-e50e24dcca9e";
export const CHR_TODO_SYNC = "6e400012-b5a3-f393-e0a9-e50e24dcca9e";
export const CHR_BATTERY_ALERT = "6e400013-b5a3-f393-e0a9-e50e24dcca9e";
export const CHR_COMPLETED_LOG = "6e400014-b5a3-f393-e0a9-e50e24dcca9e";
export const CHR_WATCHFACE_META = "6e400015-b5a3-f393-e0a9-e50e24dcca9e";
export const CHR_WATCHFACE_IMAGE = "6e400016-b5a3-f393-e0a9-e50e24dcca9e";
export const CHR_ALARM_SYNC = "6e400017-b5a3-f393-e0a9-e50e24dcca9e";

export interface DeviceInfo {
  fw: string;
  battery: number;
  charging: boolean;
  storageFree: number;
}

export type TodoPriority = 0 | 1 | 2 | 3;

export type TodoRepeat = 0 | 1;

export interface TodoItem {
  id: string;
  text: string;
  done: boolean;
  priority: TodoPriority;
  repeat: TodoRepeat;
  sortOrder: number;
  createdAt: number;
  completedAt: number;
  deleted?: boolean;
}

export interface TodoPayload {
  items: TodoItem[];
}

export interface AlarmItem {
  id: string;
  label: string;
  hour: number;
  minute: number;
  enabled: boolean;
  lastFiredYmd?: number;
  deleted?: boolean;
}

export interface AlarmPayload {
  items: AlarmItem[];
}

export interface BatteryAlert {
  level: 20 | 10 | 5;
  percent: number;
}

export type WatchfacePreset = "classic" | "minimal" | "bold";

export interface WatchfaceConfig {
  preset: WatchfacePreset;
  showDate: boolean;
  showBattery: boolean;
  hasImage?: boolean;
  bgColor: string;
  timeColor: string;
  dateColor: string;
}

export const WATCHFACE_IMAGE_W = 240;
export const WATCHFACE_IMAGE_H = 240;

export const WF_IMG_OP_BEGIN = 0x01;
export const WF_IMG_OP_DATA = 0x02;
export const WF_IMG_OP_COMMIT = 0x03;
export const WF_IMG_OP_CLEAR = 0x04;

export const TODO_OP_UPLOAD_BEGIN = 0x10;
export const TODO_OP_UPLOAD_DATA = 0x11;
export const TODO_OP_UPLOAD_COMMIT = 0x12;
export const TODO_OP_DL_PREPARE = 0x20;
export const TODO_OP_DL_PAGE = 0x21;
export const TODO_PING_CHANGED = 0x40;
export const TODO_PAGE_SIZE = 400;
