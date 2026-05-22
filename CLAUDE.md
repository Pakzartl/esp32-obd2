# ADV350 OBD Logger — Project Context

> **Project**: Honda ADV350 OBD-II data logger & analyzer
> **Hardware**: ESP32 (4MB Flash, no PSRAM) + SN65HVD230 CAN transceiver
> **Firmware**: C / ESP-IDF v5.x
> **App**: Flutter (Dart) — BLE + local SQLite
> **Cloud**: Cloudflare D1 + Workers

---

## Context for Claude Code

Embedded firmware project สำหรับ Honda ADV350 (Euro 5) อ่าน CAN bus ผ่าน OBD 6-pin connector ส่ง telemetry ผ่าน BLE ไปยัง Flutter app แล้ว sync ขึ้น Cloudflare D1

**เมื่อช่วยเขียนโค้ด ให้คำนึงถึง:**

- **Resource constraints**: ESP32 มี 4MB Flash, ไม่มี PSRAM — ใช้ memory อย่างประหยัด, static allocation เท่าที่ทำได้
- **Real-time CAN**: Honda ECU ต้อง poll (ไม่ broadcast) — UDS request/response cycle ต้องเร็ว
- **Power loss safe**: รถมอเตอร์ไซค์ตัดไฟกระทันหัน — NVS commit pattern
- **Thermal**: รถจอดแดดไทย ambient ถึง 70°C — ใช้ algorithm ที่ CPU ต่ำ
- **OTA-first**: ทุก feature ต้องคิด OTA ตั้งแต่แรก — OTA_0/OTA_1 partition, rollback support
- **READ-ONLY CAN** — ห้ามเขียน CAN frame ที่ไม่ใช่ UDS ReadDataByIdentifier (0x22)
- **Raw data integrity** — ALWAYS store raw bytes (raw_ble_hex) alongside decoded data, NEVER discard

**Coding conventions:**
- C (ESP-IDF), ไม่ใช่ Rust
- Logging: `ESP_LOGI`, `ESP_LOGW`, `ESP_LOGE` macros
- Error handling: `esp_err_t` return codes, `ESP_ERROR_CHECK` for fatal
- NimBLE for BLE peripheral
- Comments: English for technical, Thai OK for business logic

---

## Hardware

### Deployed
- [x] ESP32 DevKit (4MB Flash, no PSRAM)
- [x] SN65HVD230 CAN Transceiver (GPIO4=TX, GPIO5=RX)
- [x] OBD 6-pin Honda Euro 5 cable
- [x] FT232 USB-Serial adapter (/dev/cu.usbserial-10)
- [x] USB-C cable (data)
- [x] Breadboard + jumper wires

### Phase 2 — Production deploy
- [ ] DC-DC step-down 12V→5V/1A automotive grade
- [ ] Inline fuse holder + 1A fuse
- [ ] Waterproof enclosure IP65
- [ ] Automotive wiring + crimp connectors

---

## Architecture

```
Honda ADV350 (Euro 5)
ECU ← UDS Poll (29-bit extended CAN, 500 kbps)
     ↓ Response
┌─────────────────────────────────┐
│  ESP32 + SN65HVD230             │
│  ├── TWAI (CAN driver, 500kbps) │
│  ├── UDS engine (0x18DA10F1)    │
│  ├── OBD2 decoder (14 DIDs)     │
│  ├── Derived metrics            │
│  ├── NimBLE peripheral          │
│  ├── WiFi + HTTP server (debug) │
│  ├── NVS (config: DEV/PROD)     │
│  ├── ESP-NOW TX → S3            │
│  └── OTA (dual partition)       │
└─────────────────────────────────┘
     ↓ ESP-NOW (relay packet v2)
┌─────────────────────────────────┐
│  ESP32-S3 BLE Relay             │
│  ├── ESP-NOW RX (ch 1)          │
│  ├── NimBLE peripheral          │
│  ├── WiFi AP (ADV350-Setup)     │
│  ├── HTTP config portal         │
│  ├── SPIFFS data logger (384KB) │
│  ├── Smart features (alerts,    │
│  │   trip detect, board temp)   │
│  ├── BLE OTA (char 0xf8)        │
│  ├── BLE Mgmt (char 0xf9)       │
│  └── OTA (dual partition)       │
└─────────────────────────────────┘
     ↓ BLE GATT notify
┌─────────────────────────────────┐
│  Flutter App (Android/iOS)      │
│  ├── BLE scan + connect         │
│  ├── Vehicle data decoder       │
│  ├── SQLite (adv350.db v7)      │
│  ├── 4-tab: Ride/Trip/Vehicle/  │
│  │   Dev (board mgmt, OTA)      │
│  ├── Cloud sync → D1            │
│  └── BLE OTA firmware update    │
└─────────────────────────────────┘
     ↓ HTTPS batch POST
┌─────────────────────────────────┐
│  Cloudflare D1 + Worker         │
│  POST /api/telemetry (batch)    │
│  GET  /api/telemetry (query)    │
│  GET  /api/firmware/latest (D1) │
└─────────────────────────────────┘
```

---

## Tech Stack

| Component | Choice | Rationale |
|---|---|---|
| MCU | ESP32 (original) | TWAI built-in, BLE + WiFi, cheap, proven |
| Language | C / ESP-IDF v5.x | Direct hardware access, mature ecosystem |
| CAN driver | TWAI (ESP-IDF) | Native, hardware accelerated |
| BLE | NimBLE | Lightweight, single connection peripheral |
| App | Flutter + Dart | Cross-platform, fast UI iteration |
| App storage | SQLite (sqflite) | Local-first, offline capable |
| App BLE | flutter_blue_plus | Mature, well-documented |
| Cloud | Cloudflare D1 + Workers | Free tier: 5GB storage, 100K req/day, APAC edge |

**ที่ไม่เลือก:**
- ~~Rust + esp-idf-hal~~ — tried early, pivoted to C for faster iteration
- ~~ESP32-S3 N16R8~~ — original plan, pivoted to ESP32 for prototyping
- ~~Supabase~~ — pivoted to Cloudflare D1 for simpler deployment

---

## Honda ADV350 CAN Protocol (Confirmed)

### OBD Connector: 6-pin ISO 19689

```
Pinout (looking into connector on bike):
┌─────────────┐
│  A   B   C  │
│  D   E   F  │
└─────────────┘

A = GND (chassis ground)
B = CAN-H
C = SCS (Service Check Signal — jumper to GND for diagnostic mode)
D = K-Line (Honda HDS / KWP2000 — not used)
E = CAN-L
F = +12V (battery, switched by ignition)
```

### Protocol
- **29-bit extended CAN only** — 11-bit standard OBD2 is ignored by ECU
- **Request**: `0x18DA10F1` (Tester F1 → ECU 10)
- **Response**: `0x18DAF110` (ECU 10 → Tester F1)
- **UDS ReadDataByIdentifier** (SID 0x22) with DIDs in 0xF4xx range
- **500 kbps**, ISO-TP single frame + multi-frame
- **ECU never broadcasts** — must poll

### Confirmed DIDs — Single Frame (7)

| DID | Sensor | Formula | Unit | Verified |
|-----|--------|---------|------|----------|
| 0xF40C | RPM | (A*256+B)/4 | rpm | 2026-05-20 |
| 0xF40D | Vehicle Speed | A | km/h | 2026-05-20 |
| 0xF411 | Throttle Position | A*100/255 | % | 2026-05-20 |
| 0xF405 | Coolant Temp | A-40 | °C | 2026-05-20 |
| 0xF40B | MAP | A | kPa | 2026-05-20 |
| 0xF40F | IAT | A-40 | °C | 2026-05-20 |
| 0xF404 | Engine Load | A*100/255 | % | 2026-05-20 |

### Confirmed DIDs — Multi-frame ISO-TP (2)

| DID | Sensor | Response Size | Verified |
|-----|--------|---------------|----------|
| 0x0100 | Monitor Status (MIL + DTC count) | 59 bytes | 2026-05-20 |
| 0x0124 | Lambda (O2 sensor) | multi-frame | 2026-05-20 |

### Not Available (NRC 0x31 — requestOutOfRange)

0xF40E (Ignition Timing), 0xF406 (Short Fuel Trim), 0xF407 (Long Fuel Trim),
0xF41C (OBD Compliance), 0xF403 (Fuel System), 0xF402 (Freeze DTC),
0xF442 (Battery Voltage), 0xF45E (Fuel Rate)

---

## Project Structure

```
adv350-logger/
├── CLAUDE.md
├── firmware/           # ACTIVE firmware (ESP32, C, ESP-IDF)
│   ├── main/
│   │   ├── main.c             # Entry: TWAI, BLE, WiFi, UDS poll, HTTP server
│   │   ├── obd2.c             # Honda UDS engine (29-bit extended CAN)
│   │   ├── obd2.h             # DID definitions, vehicle constants
│   │   └── metrics.c          # Derived: fuel consumption, accel, g-force, riding score
│   ├── CMakeLists.txt
│   ├── sdkconfig.defaults     # 4MB flash, NimBLE, TWAI, WiFi, OTA
│   └── partitions.csv         # OTA_0 + OTA_1 (1.75MB each)
├── firmware-s3/               # ESP32-S3 BLE Relay firmware
│   ├── main/
│   │   ├── main.c             # Entry: ESP-NOW, BLE, WiFi, status task
│   │   ├── espnow_rx.c/h     # ESP-NOW receiver, APSTA WiFi
│   │   ├── ble_relay.c/h     # BLE GATT server, vehicle data notify
│   │   ├── relay_packet.h    # Wire format v2 (ESP32↔S3)
│   │   ├── smart_features.c/h # Alerts, trip detect, board temp
│   │   ├── wifi_portal.c/h   # HTTP config portal on AP
│   │   ├── flash_logger.c/h  # SPIFFS telemetry logger
│   │   └── ble_ota.c/h       # BLE OTA firmware update
│   ├── partitions.csv         # OTA_0 + OTA_1 + storage (384KB)
│   └── sdkconfig.defaults
├── app/                       # Flutter app
│   ├── lib/
│   │   ├── main.dart
│   │   ├── services/
│   │   │   ├── ble_service.dart       # GATT connect, notifications, mgmt, OTA
│   │   │   ├── database_service.dart  # SQLite CRUD (v7: board_temp)
│   │   │   ├── cloud_sync_service.dart # Cloudflare D1 batch sync
│   │   │   └── ota_service.dart       # Firmware update check + BLE OTA
│   │   ├── models/
│   │   │   └── telemetry.dart         # Vehicle data decoder, raw_ble_hex
│   │   └── screens/
│   │       ├── scan_screen.dart
│   │       ├── dashboard_screen.dart  # 4-tab layout
│   │       ├── tabs/ride_tab.dart     # Live gauges + fuel hint
│   │       ├── tabs/trip_tab.dart     # Trip metrics, sparklines
│   │       ├── tabs/vehicle_tab.dart  # All sensors + board temp
│   │       ├── tabs/dev_tab.dart      # BLE mgmt, cloud sync, OTA, logs
│   │       ├── ota_screen.dart        # Firmware update UI
│   │       ├── raw_log_screen.dart
│   │       └── history_screen.dart
│   └── pubspec.yaml
├── cloud/                     # Cloudflare D1 + Worker API
│   ├── src/index.ts           # Worker: telemetry + firmware API
│   ├── schema.sql             # D1 tables (telemetry + firmware)
│   ├── wrangler.jsonc         # D1 binding config
│   └── package.json
├── docs/
│   └── can-ids.md             # Complete Honda ADV350 CAN RE documentation
```

---

## Key Configuration

### `firmware/sdkconfig.defaults`
```
# Flash (4MB)
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="4MB"

# Partition (custom OTA)
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"

# BLE (NimBLE, peripheral only)
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y

# WiFi
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=4
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=8

# TWAI (CAN)
CONFIG_TWAI_ERRATA_FIX_BUS_OFF_REC=y
CONFIG_TWAI_ERRATA_FIX_TX_INTR_LOST=y
CONFIG_TWAI_ERRATA_FIX_RX_FRAME_INVALID=y
CONFIG_TWAI_ERRATA_FIX_RX_FIFO_CORRUPT=y

# OTA
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y

# Brownout (disabled for bench — USB power insufficient)
CONFIG_ESP_BROWNOUT_DET=n
```

### `firmware/partitions.csv`
```
# Name,    Type, SubType,  Offset,    Size
nvs,       data, nvs,      0x9000,    0x6000     (24 KB)
otadata,   data, ota,      0xf000,    0x2000     (8 KB)
phy_init,  data, phy,      0x11000,   0x1000     (4 KB)
ota_0,     app,  ota_0,    0x20000,   0x1C0000   (1.75 MB)
ota_1,     app,  ota_1,    0x1E0000,  0x1C0000   (1.75 MB)
```

---

## BLE Protocol (S3 Relay)

- **Service UUID**: `12345678-1234-5678-1234-56789abcdef0`
- **Frame char**: `...def1` (R/notify — raw CAN/UDS frames, placeholder)
- **Control char**: `...def2` (R/W — notify toggle, forward cmds to CAN board)
- **Vehicle data char**: `...def3` (R/notify — 17 bytes: flags+sensors+boardTemp)
- **Metrics char**: `...def4` (R/notify — 16 bytes: 8×int16 derived metrics)
- **DTC char**: `...def5` (R — placeholder)
- **FW version char**: `...def7` (R — version string e.g. "0.5.1-relay")
- **OTA char**: `...def8` (R/W/notify — firmware update: 0x01=begin, 0x02=data, 0x03=end, 0x04=abort)
- **Mgmt char**: `...def9` (R/W — 16-byte board info read, 0x01=clear logs, 0x02=restart)

### Vehicle Data Packet (17 bytes)
```
[0-1] vd_flags (uint16 LE)    [9-10] battery_cv (uint16 LE)
[2-3] rpm (uint16 LE)         [11-12] fuel_rate_x100 (uint16 LE)
[4]   speed (uint8)           [13-14] cvt_x100 (uint16 LE)
[5]   coolant+40 (uint8)      [15] riding_score (uint8)
[6]   throttle*2.55 (uint8)   [16] board_temp+40 (uint8)
[7]   map_kpa (uint8)
[8]   iat+40 (uint8)
```

---

## NVS Modes

Firmware (ESP32 OBD2) switches behavior via NVS `mode` key:
- **DEV**: WiFi enabled, HTTP server at `http://adv350.local`
  - `/api/frames` — live CAN frames + TWAI status
  - `/api/scan?range=xx&go=1` — DID brute-force scanner
  - `/log` — CAN log viewer with live JS polling
  - `/ota` — firmware update page (browse .bin file)
  - `/update` — POST multipart firmware binary (used by curl OTA)
- **PROD**: BLE only, WiFi off, optimized for app connection

### S3 Relay WiFi Portal
- **AP SSID**: ADV350-Setup (WPA2)
- **URL**: `http://192.168.4.1`
- Status dashboard, logger stats, config, restart

---

## Useful Commands

```bash
# ESP-IDF environment (run once per terminal)
export IDF_PATH=$HOME/esp/esp-idf && . $IDF_PATH/export.sh

# Monitor serial output (no flash)
idf.py -p /dev/cu.usbserial-10 monitor       # ESP32 OBD2
idf.py -p /dev/cu.usbmodem1101 monitor        # ESP32-S3 Relay

# Find USB device
find /dev -name "cu.usb*" -maxdepth 1

# Manual reset to bootloader: hold BOOT + press RESET + release BOOT

# Flutter dev
cd app && flutter pub get && flutter run

# Cloudflare Worker dev
cd cloud && bun install && bun run dev
```

Build, flash, release commands → see **Release Workflow** section below

---

## Critical Constraints

### CAN
- **READ-ONLY** — only UDS ReadDataByIdentifier (0x22), no write commands
- **Bus-off recovery** — TWAI errata fixes enabled, auto-recover
- **Bitrate**: 500 kbps only — other rates confirmed not working (250k → bus errors)

### Power
- **Brownout on bench** — BLE init draws ~240mA peak, FT232 3V3 can't supply
- **Fix**: `CONFIG_ESP_BROWNOUT_DET=n` for bench, vehicle 12V has no issue
- **Battery drain** — deep sleep or relay cutoff needed for parking

### Data
- **ALWAYS store raw_ble_hex** — past data loss from discarding raw bytes (3,580 rows lost)
- **NVS write ~100K cycles** — don't write frequently
- **OTA rollback** — mark valid within N seconds after first boot, else auto-rollback

### BLE
- **Connection drops after ~30s** in PROD mode — under investigation
- **Single connection only** — NimBLE configured for 1 peripheral connection

---

## Security

- **WiFi creds** stored in NVS (not in git) — via sdkconfig.local or NVS write
- **Cloudflare API key** — Worker secret, not in wrangler.jsonc
- **BLE pairing** — LE Secure Connections + bonding (production target)
- **OTA signing** — secure boot + signed images (production target)

---

## Definition of Done (v1.0)

- [x] Read RPM, speed, throttle, coolant temp via UDS (14 DIDs confirmed)
- [ ] Record real driving data with decoded values + raw_ble_hex
- [x] Cloud sync to Cloudflare D1 (batch upload from app) — 2,936 rows synced
- [x] OTA update — BLE OTA via char 0xf8 + firmware API on D1
- [ ] Run 7 days continuous on bike without crash
- [x] App redesign: 4-tab layout (Ride/Trip/Vehicle/Dev)
- [ ] BLE stable connection (fix 30s dropout)
- [x] S3 WiFi config portal (AP: ADV350-Setup, WPA2)
- [x] S3 SPIFFS flash data logger (384KB, auto-log during trips)
- [x] S3 board temp in BLE + app Vehicle tab
- [x] S3 BLE management characteristic (board info, clear logs, restart)

---

## Release Workflow

โปรเจคนี้มี 3 component ที่ release แยกกัน:

| Component | Repo | Versioning | Delivery |
|-----------|------|------------|----------|
| firmware-s3 (BLE Relay) | Pakzartl/esp32-obd2 | git tag → `FW_VERSION_S3` | BLE OTA via app |
| firmware (ESP32 OBD2) | Pakzartl/esp32-obd2 | git tag → `FW_VERSION` | WiFi OTA (`curl`) |
| app (Flutter) | Pakzartl/obd2-flutter | pubspec.yaml version | APK sideload / in-app self-update |

### How Versioning Works

Firmware version ดึงจาก git tag อัตโนมัติผ่าน CMake:
```
git describe --tags --always --dirty  →  "0.2.4" (strip "v" prefix)
```
- Tag `v0.2.4` → firmware reports `0.2.4-relay`
- ไม่มี tag → fallback `0.0.0`
- Override ได้: `idf.py build -DFORCE_FW_VER=0.2.4`

### How App Discovers New Firmware

```
App → GET /api/firmware/latest?component=firmware-s3
    → Cloudflare Worker → D1 query: SELECT ... FROM firmware WHERE component=? ORDER BY id DESC LIMIT 1
    → Returns: { version, changelog, download_url, size }
    → App compares with current board version → prompt user to update via BLE OTA
```

ไม่ต้อง redeploy Worker — แค่ INSERT row ใหม่ใน D1 `firmware` table

---

### Release: firmware-s3 (BLE Relay)

ใช้บ่อยที่สุด เพราะ S3 เป็น board ที่ user เห็นผ่าน app

#### Step 1 — Commit + Tag (ก่อน build!)

```bash
git add -A
git commit -m "feat: short description of changes"
git tag v0.X.Y
```

Tag format: `v{major}.{minor}.{patch}` เช่น `v0.2.5`

**สำคัญ**: ต้อง tag ก่อน build เพราะ CMake ใช้ `git describe --tags` ดึง version
ถ้า build ก่อน tag → binary จะได้ version เก่า เช่น `0.2.3-1-g3aa7dd4-relay` แทนที่จะเป็น `0.2.4-relay`

#### Step 2 — Build

```bash
export IDF_PATH=$HOME/esp/esp-idf && . $IDF_PATH/export.sh
cd firmware-s3 && idf.py build
```

Output binary: `firmware-s3/build/adv350-s3-relay.bin`
ยืนยัน version ใน build log: `Firmware version: 0.X.Y-relay`

#### Step 3 — Push + GitHub Release

```bash
git push origin dev --tags
```

```bash
# Get binary size
BIN_SIZE=$(stat -f%z firmware-s3/build/adv350-s3-relay.bin)

# Get SHA256
SHA256=$(shasum -a 256 firmware-s3/build/adv350-s3-relay.bin | cut -d' ' -f1)

# Create release + upload binary
gh release create v0.X.Y \
  --repo Pakzartl/esp32-obd2 \
  --title "v0.X.Y — short description" \
  --notes "$(cat <<'EOF'
- Change 1
- Change 2

Binary: adv350-s3-relay.bin
Size: SIZE bytes
SHA256: HASH
EOF
)" \
  firmware-s3/build/adv350-s3-relay.bin
```

#### Step 5 — Register in D1

```bash
cd cloud && bunx wrangler d1 execute adv350-telemetry --remote \
  --command "INSERT INTO firmware (component, version, changelog, download_url, size) \
  VALUES ( \
    'firmware-s3', \
    '0.X.Y', \
    'Short changelog', \
    'https://github.com/Pakzartl/esp32-obd2/releases/download/v0.X.Y/adv350-s3-relay.bin', \
    BIN_SIZE \
  )"
```

D1 `firmware` table schema:
```
id INTEGER PRIMARY KEY AUTOINCREMENT
component TEXT (default: 'firmware-s3')
version TEXT
changelog TEXT
download_url TEXT
size INTEGER
created_at TEXT (auto)
```

#### Step 6 — Verify + OTA

```bash
# API returns new version
curl -s https://adv350-api.pakzartl.workers.dev/api/firmware/latest?component=firmware-s3 | jq .

# Expected: { "component": "firmware-s3", "version": "0.X.Y", ... }
```

1. เปิด app → System screen → ดู "New firmware available" ขึ้น
2. กด Update → app download .bin จาก GitHub → ส่งผ่าน BLE OTA (char 0xf8)
3. Board reboot อัตโนมัติ → app reconnect → ดู version ใหม่ใน System screen
4. Board จะ mark OTA valid อัตโนมัติ — ถ้าไม่ mark จะ rollback กลับ version เดิม

---

### Release: firmware (ESP32 OBD2)

Board CAN/UDS ตัวหลัก — release น้อยกว่า S3, update ผ่าน WiFi OTA

#### Step 1 — Build + OTA Flash

```bash
export IDF_PATH=$HOME/esp/esp-idf && . $IDF_PATH/export.sh
cd firmware && idf.py build

# WiFi OTA (Board ต้องอยู่ DEV mode, WiFi enabled)
curl -X POST -F "firmware=@build/can_sniffer.bin" http://adv350.local/update
# Board reboots อัตโนมัติ — ping adv350.local รอจนตอบ (~5s)
```

#### Step 2 — GitHub Release (ถ้าจะ publish)

```bash
gh release create v0.X.Y \
  --repo Pakzartl/esp32-obd2 \
  --title "v0.X.Y — OBD2 firmware description" \
  --notes "changelog" \
  firmware/build/can_sniffer.bin
```

Register ใน D1 ด้วย `component='firmware'`:
```bash
cd cloud && bunx wrangler d1 execute adv350-telemetry --remote \
  --command "INSERT INTO firmware (component, version, changelog, download_url, size) \
  VALUES ('firmware', '0.X.Y', 'changelog', 'URL', SIZE)"
```

---

### Release: Flutter App

App อยู่ repo แยก: `Pakzartl/obd2-flutter`

```bash
cd app
flutter build apk --release
# Output: build/app/outputs/flutter-apk/app-release.apk
```

App มี in-app self-update — ตรวจ GitHub releases ของ repo ตัวเอง

---

### Cloudflare Worker (cloud/)

Worker ไม่ต้อง deploy ทุกครั้งที่ release firmware — แค่ INSERT row ใน D1

Deploy เฉพาะเมื่อเปลี่ยน Worker code:
```bash
cd cloud
bun install
bun run deploy
```

Schema migration:
```bash
bunx wrangler d1 execute adv350-telemetry --remote --file schema.sql
```

Query firmware table:
```bash
bunx wrangler d1 execute adv350-telemetry --remote \
  --command "SELECT * FROM firmware ORDER BY id DESC LIMIT 5"
```

---

### Quick Reference: S3 Release (copy-paste)

```bash
# 1. Commit + tag (ก่อน build!)
git add -A && git commit -m "feat: description"
git tag v0.X.Y

# 2. Build (หลัง tag — ให้ git describe ได้ version ถูก)
export IDF_PATH=$HOME/esp/esp-idf && . $IDF_PATH/export.sh
cd firmware-s3 && idf.py build
# ดู build log: "Firmware version: 0.X.Y-relay"

# 3. Push + GitHub release
git push origin dev --tags
gh release create v0.X.Y \
  --repo Pakzartl/esp32-obd2 \
  --title "v0.X.Y — description" \
  --notes "changelog" \
  firmware-s3/build/adv350-s3-relay.bin

# 4. Register in D1
BIN_SIZE=$(stat -f%z firmware-s3/build/adv350-s3-relay.bin)
cd cloud && bunx wrangler d1 execute adv350-telemetry --remote \
  --command "INSERT INTO firmware (component,version,changelog,download_url,size) \
  VALUES ('firmware-s3','0.X.Y','changelog','https://github.com/Pakzartl/esp32-obd2/releases/download/v0.X.Y/adv350-s3-relay.bin',$BIN_SIZE)"

# 5. Verify API
curl -s "https://adv350-api.pakzartl.workers.dev/api/firmware/latest" | jq .

# 6. OTA via app → System screen → Update firmware

# 6. Verify
curl -s https://adv350-api.pakzartl.workers.dev/api/firmware/latest | jq .
```

---

*Last updated: 2026-05-22*
