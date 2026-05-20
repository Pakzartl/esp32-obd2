# ADV350 OBD Logger

Real-time OBD-II data logger for Honda ADV350 (Euro 5). Reads CAN bus via UDS protocol, streams over BLE to a Flutter app, syncs to Cloudflare D1.

```
Honda ADV350 ECU ──CAN 500kbps──► ESP32 ──BLE──► Flutter App ──HTTPS──► Cloudflare D1
```

## Architecture

| Component | Stack | Description |
|-----------|-------|-------------|
| **Firmware** | C / ESP-IDF v5.x | UDS engine, ISO-TP multi-frame, NimBLE, OTA |
| **App** | Flutter / Dart | BLE dashboard, SQLite, raw backup, debug server |
| **Cloud** | Cloudflare D1 + Workers | Batch telemetry API, firmware update proxy |

## Confirmed Sensors (Honda ADV350)

| DID | Sensor | Unit |
|-----|--------|------|
| 0xF40C | RPM | rpm |
| 0xF40D | Vehicle Speed | km/h |
| 0xF411 | Throttle Position | % |
| 0xF405 | Coolant Temp | °C |
| 0xF40B | MAP | kPa |
| 0xF40F | Intake Air Temp | °C |
| 0xF404 | Engine Load | % |
| 0x0100 | Monitor Status (MIL) | multi-frame |
| 0x0124 | Lambda (O2) | multi-frame |

## Hardware

- ESP32 DevKit (4MB Flash, no PSRAM)
- SN65HVD230 CAN transceiver (GPIO4=TX, GPIO5=RX)
- Honda OBD 6-pin connector (CAN-H pin B, CAN-L pin E)
- 29-bit extended CAN, 500 kbps

## Quick Start

```bash
make setup        # Install all dependencies
make firmware     # Build ESP32 firmware
make flash        # Flash via USB
make app          # Build Flutter APK (release)
make install      # Install APK to connected device
make deploy       # Deploy Cloudflare Worker
make dev          # Run Flutter app in debug mode
```

## Debug Server

Enable Developer Mode in app Settings to start HTTP debug server on port 8350.

```bash
adb forward tcp:8350 tcp:8350

# Query database
curl localhost:8350/db/count
curl localhost:8350/db/recent?limit=10
curl -X POST localhost:8350/db/sql -d '{"sql":"SELECT * FROM telemetry LIMIT 5"}'

# Export
curl -o adv350.db localhost:8350/db/export
curl localhost:8350/raw/list
curl -o backup.jsonl localhost:8350/raw/pull/<filename>
```

## OTA Firmware Update

1. Build firmware: `make firmware`
2. Create GitHub release: `make release VERSION=x.y.z`
3. Open app → OTA screen → Download & Flash via BLE

## Project Structure

```
adv350-logger/
├── firmware/              # ESP32 firmware (C, ESP-IDF)
│   ├── main/
│   │   ├── main.c         # Entry: TWAI, BLE, WiFi, UDS, HTTP server
│   │   ├── obd2.c         # Honda UDS engine + ISO-TP multi-frame
│   │   ├── obd2.h         # DID definitions, vehicle data structs
│   │   └── metrics.c      # Derived: fuel consumption, g-force, riding score
│   ├── partitions.csv     # OTA_0 + OTA_1 (1.75MB each)
│   └── sdkconfig.defaults
├── app/                   # Flutter app
│   └── lib/
│       ├── main.dart
│       ├── models/        # Telemetry decoder
│       ├── screens/       # Dashboard, Metrics, OTA, Settings, History
│       ├── services/      # BLE, Database, Debug Server, Raw Backup, OTA
│       └── widgets/       # Gauge cards
├── cloud/                 # Cloudflare Worker
│   ├── src/index.ts       # API: telemetry batch, firmware proxy
│   └── schema.sql         # D1 schema
├── CLAUDE.md              # AI context for development
└── Makefile
```

## CAN Protocol

- 29-bit extended CAN only (11-bit ignored by ECU)
- Request: `0x18DA10F1` (Tester → ECU)
- Response: `0x18DAF110` (ECU → Tester)
- UDS ReadDataByIdentifier (SID 0x22)
- ISO-TP single frame + multi-frame (Flow Control)
- **READ-ONLY** — only SID 0x22, no write commands

## License

Private project.
