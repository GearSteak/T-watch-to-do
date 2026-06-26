# T-Watch S3 Companion

Firmware + web companion for the LilyGO T-Watch S3.

## Phase 1 features

- To-do list on watch (check off tasks) + web page sync via Bluetooth
- Set watch time from the web page
- Battery percentage on watch and web
- Low-battery alerts at 20%, 10%, and 5%

## Flash firmware

1. Install [PlatformIO](https://platformio.org/) (VS Code extension or CLI).
2. Connect the T-Watch S3 via USB.
3. From this folder:

```bash
pio run -e twatchs3 -t upload
pio device monitor
```

If upload fails, hold **BOOT**, tap **RST**, release **RST** while holding **BOOT**, then upload.

## Web companion

```bash
cd web
npm install
npm run dev
```

Open `http://localhost:5173` in **Chrome** or **Edge**, click **Connect via Bluetooth**, and select `TWatch-Companion`.

> **iPhone note:** Safari does not support Web Bluetooth. WiFi setup mode (Phase 2) or the Bluefy browser can be used as a workaround.

## Project layout

```
src/           Firmware (PlatformIO + LilyGoLib + LVGL + NimBLE)
web/           Web companion (Vite + TypeScript + Web Bluetooth)
boards/        T-Watch S3 board definition
```

## BLE service

| Characteristic | Purpose |
|----------------|---------|
| `device_info` | Battery %, charging, firmware version |
| `time_sync` | Set RTC from browser |
| `todo_sync` | Bidirectional todo JSON sync |
| `battery_alert` | Low-battery notifications |
| `completed_log` | Recently completed tasks |

## Roadmap

- Phase 2: Alarms + WiFi setup mode (iPhone)
- Phase 3: Google Calendar sync
- Phase 4: Watch face editor
- Phase 5: iPhone notifications (ANCS)
