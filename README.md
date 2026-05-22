# ADV350 OBD Logger

**Latest: v0.1.0** | [Release](https://github.com/Pakzartl/esp32-obd2/releases/latest)

Open-source real-time OBD-II data logger for **Honda ADV350 (Euro 5)**. Reads CAN bus via UDS protocol, relays through ESP32-S3 over BLE to a Flutter dashboard app, with cloud sync to Cloudflare D1.

```
ADV350 ECU ←CAN 500kbps→ ESP32 ═ESP-NOW 10Hz═> ESP32-S3 ←BLE→ Flutter App → SQLite → Cloudflare D1
```

## Features

- **Real-time telemetry**: RPM, Speed, Throttle, Coolant, MAP, IAT, Fuel Rate, CVT Ratio, Riding Score
- **ESP32-S3 BLE relay**: dedicated relay board with 0% packet loss via unicast ESP-NOW
- **4-tab Flutter app**: Ride (glanceable gauges), Trip (sparkline charts + datetime picker), Vehicle (all sensors), Dev (raw log, OTA, debug)
- **DID discovery**: probe + brute-force scanner for Honda Keihin ECU
- **OTA firmware update**: via WiFi (DEV mode) or BLE (PROD mode)
- **Offline-first**: SQLite local storage with Cloudflare D1 batch sync
- **Raw data integrity**: every BLE packet stored as hex for post-processing

## Architecture

| Component | Stack | Description |
|-----------|-------|-------------|
| **Firmware** | C / ESP-IDF v5.5 | UDS engine, ISO-TP, ESP-NOW sender, NimBLE, OTA |
| **Firmware S3** | C / ESP-IDF v5.5 | ESP-NOW receiver, BLE GATT relay |
| **App** | Flutter / Dart | BLE dashboard, SQLite, sparklines, raw backup |
| **Cloud** | Cloudflare D1 + Workers | Batch telemetry API, firmware update proxy |

## Confirmed Sensors (Honda ADV350 Euro 5)

| DID | Sensor | Unit | Source |
|-----|--------|------|--------|
| 0xF40C | RPM | rpm | ECU |
| 0xF40D | Vehicle Speed | km/h | ECU |
| 0xF411 | Throttle Position | % | ECU |
| 0xF405 | Coolant Temp | °C | ECU |
| 0xF40B | MAP (Manifold Absolute Pressure) | kPa | ECU |
| 0xF40F | Intake Air Temp | °C | ECU |
| 0xF404 | Engine Load | % | ECU |
| 0xF40E | Ignition Timing | ° | ECU |
| 0xF406 | Short Fuel Trim | % | ECU |
| 0x0100 | Monitor Status (MIL + DTC) | multi-frame | ECU |
| 0x0124 | Lambda (O2 sensor) | multi-frame | ECU |
| — | Fuel Rate | L/h | Calculated (Speed-Density) |
| — | CVT Ratio | ratio | Calculated |
| — | Riding Score | 0-100 | Calculated |

## Hardware

### CAN Board (ESP32)
- ESP32 DevKit (4MB Flash, no PSRAM)
- SN65HVD230 CAN transceiver (GPIO26=TX, GPIO27=RX)
- Honda OBD 6-pin connector (CAN-H pin B, CAN-L pin E)

### BLE Relay (ESP32-S3)
- ESP32-S3 DevKit (4MB+ Flash)
- No external components — receives data wirelessly via ESP-NOW

### OBD Connector Pinout
```
┌─────────────┐
│  A   B   C  │   A = GND
│  D   E   F  │   B = CAN-H    E = CAN-L
└─────────────┘   C = SCS       F = +12V (ignition)
                  D = K-Line (unused)
```

## Quick Start

### Prerequisites
- [ESP-IDF v5.5+](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/)
- [Flutter SDK](https://flutter.dev/docs/get-started/install)
- Android device with BLE support

### Build & Flash

```bash
# ESP-IDF environment
export IDF_PATH=$HOME/esp/esp-idf
. $IDF_PATH/export.sh

# ESP32 CAN board
cd firmware
idf.py set-target esp32
idf.py build
idf.py -p /dev/cu.usbserial-10 flash monitor

# ESP32-S3 relay
cd firmware-s3
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem101 flash monitor

# Flutter app
cd app
flutter pub get
flutter build apk --release
# or: flutter run
```

### WiFi Credentials (ESP32 CAN Board)

WiFi credentials are stored in NVS (not compiled into the binary). To set them for DEV mode:

```bash
# Via ESP-IDF NVS partition tool, or write from code:
# wifi_save_creds("YourSSID", "YourPassword");
```

### OTA Firmware Update

```bash
# Switch ESP32 to DEV mode (hold IO0 button 5 seconds)
# Then flash over WiFi:
curl -F "firmware=@firmware/build/can_sniffer.bin" http://adv350.local/update
# Switch back to PROD mode (hold IO0 again)
```

## Project Structure

```
adv350-logger/
├── firmware/                # ESP32 CAN board (C, ESP-IDF)
│   ├── main/
│   │   ├── main.c           # TWAI, BLE, WiFi, UDS poll, ESP-NOW TX, HTTP server
│   │   ├── obd2.c           # Honda UDS engine + ISO-TP multi-frame
│   │   ├── obd2.h           # DID definitions, vehicle data structs
│   │   ├── metrics.c        # Derived: fuel rate, g-force, CVT, riding score
│   │   ├── espnow_tx.c      # ESP-NOW unicast sender (10Hz)
│   │   └── relay_packet.h   # Shared wire format (ESP-NOW packet)
│   ├── partitions.csv
│   └── sdkconfig.defaults
├── firmware-s3/             # ESP32-S3 BLE relay
│   ├── main/
│   │   ├── main.c           # Entry + status logging
│   │   ├── ble_relay.c      # NimBLE GATT (same UUIDs as original)
│   │   ├── espnow_rx.c      # ESP-NOW receiver + auto peer discovery
│   │   └── relay_packet.h   # Shared wire format
│   ├── partitions.csv
│   └── sdkconfig.defaults
├── app/                     # Flutter app (Android)
│   └── lib/
│       ├── main.dart         # Auto-connect, dark theme
│       ├── models/           # Telemetry decoder (UDS + vehicle_data formats)
│       ├── screens/tabs/     # Ride, Trip, Vehicle, Dev
│       ├── services/         # BLE, Database, Debug Server, OTA, Raw Backup
│       └── widgets/          # Gauge cards
├── cloud/                   # Cloudflare Worker + D1
│   ├── src/index.ts
│   └── schema.sql
├── CLAUDE.md
├── Makefile
└── README.md
```

## CAN Protocol

- **29-bit extended CAN only** — 11-bit standard is ignored by Honda ECU
- Request: `0x18DA10F1` (Tester F1 → ECU 10)
- Response: `0x18DAF110` (ECU 10 → Tester F1)
- UDS ReadDataByIdentifier (SID 0x22) with DIDs in 0xF4xx range
- ISO-TP single frame + multi-frame (First Frame / Flow Control / Consecutive)
- 500 kbps, **READ-ONLY** — only SID 0x22, no write commands

## Debug Server

Enable Developer Mode in app Settings to start HTTP debug server on port 8350:

```bash
adb forward tcp:8350 tcp:8350

curl localhost:8350/db/count
curl localhost:8350/db/recent?limit=10
curl -X POST localhost:8350/db/sql -d '{"sql":"SELECT * FROM telemetry LIMIT 5"}'
curl -o adv350.db localhost:8350/db/export
```

## Contributing

Contributions welcome! This project is focused on Honda ADV350 but the UDS/CAN architecture should work with other Honda motorcycles using the same Keihin ECU platform (Euro 5).

Key areas:
- DID discovery for other Honda models
- iOS Flutter app testing
- Cloud dashboard / web viewer
- GPS integration for ride tracking

## License

MIT
