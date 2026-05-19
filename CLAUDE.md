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
│  └── OTA (dual partition)       │
└─────────────────────────────────┘
     ↓ BLE GATT notify
┌─────────────────────────────────┐
│  Flutter App (Android/iOS)      │
│  ├── BLE scan + connect         │
│  ├── UDS response decoder       │
│  ├── SQLite (adv350.db)         │
│  ├── Dashboard (decoded gauges) │
│  ├── Metrics (MAX/AVG/MIN)      │
│  └── Cloud sync → D1            │
└─────────────────────────────────┘
     ↓ HTTPS batch POST
┌─────────────────────────────────┐
│  Cloudflare D1 + Worker         │
│  POST /api/telemetry (batch)    │
│  GET  /api/telemetry (query)    │
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
- ~~Rust + esp-idf-hal~~ — tried early (firmware/ dir), pivoted to C for faster iteration
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
- **500 kbps**, ISO-TP single frame
- **ECU never broadcasts** — must poll

### Confirmed DIDs (14)

| DID | Sensor | Formula | Unit |
|-----|--------|---------|------|
| 0xF40C | RPM | (A*256+B)/4 | rpm |
| 0xF40D | Vehicle Speed | A | km/h |
| 0xF411 | Throttle Position | A*100/255 | % |
| 0xF405 | Coolant Temp | A-40 | °C |
| 0xF40B | MAP | A | kPa |
| 0xF40F | IAT | A-40 | °C |
| 0xF404 | Engine Load | A*100/255 | % |
| 0xF40E | Ignition Timing | A/2-64 | ° |
| 0xF406 | Short Fuel Trim | (A-128)*100/128 | % |
| 0xF407 | Long Fuel Trim | (A-128)*100/128 | % |
| 0xF41C | OBD Compliance | A | enum |
| 0xF403 | Fuel System | A | enum |
| 0xF401 | Monitor Status | A | bitmask |
| 0xF402 | Freeze DTC | A*256+B | code |

**Not available**: Battery voltage, fuel rate (proprietary ranges scanned, empty)

---

## Project Structure

```
adv350-logger/
├── CLAUDE.md
├── poc-can-sniffer/           # ACTIVE firmware (ESP32, C, ESP-IDF)
│   ├── main/
│   │   ├── main.c             # Entry: TWAI, BLE, WiFi, UDS poll, HTTP server
│   │   ├── obd2.c             # Honda UDS engine (29-bit extended CAN)
│   │   ├── obd2.h             # DID definitions, vehicle constants
│   │   └── metrics.c          # Derived: fuel consumption, accel, g-force, riding score
│   ├── CMakeLists.txt
│   ├── sdkconfig.defaults     # 4MB flash, NimBLE, TWAI, WiFi, OTA
│   └── partitions.csv         # OTA_0 + OTA_1 (1.75MB each)
├── firmware/                  # INACTIVE — Rust/ESP32-S3 loopback test (legacy)
├── app/                       # Flutter app
│   ├── lib/
│   │   ├── main.dart
│   │   ├── services/
│   │   │   ├── ble_service.dart       # GATT connect, notifications
│   │   │   └── database_service.dart  # SQLite CRUD
│   │   ├── models/
│   │   │   └── telemetry.dart         # UDS decoder, raw_ble_hex storage
│   │   └── screens/
│   │       ├── scan_screen.dart
│   │       ├── dashboard_screen.dart  # Decoded gauges, auto-save 1Hz
│   │       ├── metrics_screen.dart    # Trip metrics, sparklines
│   │       ├── raw_log_screen.dart
│   │       └── history_screen.dart
│   └── pubspec.yaml           # flutter_blue_plus, sqflite, shared_preferences
├── cloud/                     # Cloudflare D1 + Worker API
│   ├── src/index.ts           # Worker: batch insert + query
│   ├── schema.sql             # D1 telemetry table
│   ├── wrangler.jsonc         # D1 binding config
│   └── package.json
├── docs/
│   └── can-ids.md             # Complete Honda ADV350 CAN RE documentation
└── backups/                   # Old firmware snapshots
```

---

## Key Configuration

### `poc-can-sniffer/sdkconfig.defaults`
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

### `poc-can-sniffer/partitions.csv`
```
# Name,    Type, SubType,  Offset,    Size
nvs,       data, nvs,      0x9000,    0x6000     (24 KB)
otadata,   data, ota,      0xf000,    0x2000     (8 KB)
phy_init,  data, phy,      0x11000,   0x1000     (4 KB)
ota_0,     app,  ota_0,    0x20000,   0x1C0000   (1.75 MB)
ota_1,     app,  ota_1,    0x1E0000,  0x1C0000   (1.75 MB)
```

---

## BLE Protocol

- **Service UUID**: `12345678-1234-5678-1234-56789abcdef0`
- **Frame characteristic**: `...def1` (notify — raw CAN/UDS frames)
- **Control characteristic**: `...def2` (write — commands to firmware)
- **Data format**: Raw BLE hex packets, UDS response (0x62 = ReadDataByIdentifier response)

---

## NVS Modes

Firmware switches behavior via NVS `mode` key:
- **DEV**: WiFi enabled, HTTP debug server at `http://esp32-obd2.local`
  - `/api/frames` — live CAN frames + TWAI status
  - `/api/scan?range=xx&go=1` — DID brute-force scanner
  - `/log` — CAN log viewer with live JS polling
- **PROD**: BLE only, WiFi off, optimized for app connection

---

## Useful Commands

### Firmware
```bash
# Setup ESP-IDF environment
export IDF_PATH=$HOME/esp/esp-idf
. $IDF_PATH/export.sh

# Build
cd poc-can-sniffer && idf.py build

# Flash + monitor
idf.py -p /dev/cu.usbserial-10 flash monitor

# Monitor only
idf.py -p /dev/cu.usbserial-10 monitor

# Erase flash (factory reset)
idf.py -p /dev/cu.usbserial-10 erase-flash

# OTA via HTTP (when WiFi mode)
idf.py -p /dev/cu.usbserial-10 ota --port 8070
```

### Flutter App
```bash
cd app
flutter pub get
flutter run                    # Debug
flutter build apk --release    # Release APK
```

### Cloudflare Worker
```bash
cd cloud
bun install
bun run dev                    # Local dev
bun run deploy                 # Deploy to Cloudflare
bun run db:migrate             # Apply schema to D1
```

### Debugging
```bash
# Find USB device
find /dev -name "cu.usb*" -maxdepth 1

# Manual reset to bootloader
# Hold BOOT, press RESET, release BOOT
```

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
- [ ] Cloud sync to Cloudflare D1 (batch upload from app)
- [ ] OTA update working end-to-end
- [ ] Run 7 days continuous on bike without crash
- [ ] App redesign: 4-tab layout (Ride/Trip/Vehicle/Dev)
- [ ] BLE stable connection (fix 30s dropout)

---

*Last updated: 2026-05-19*
