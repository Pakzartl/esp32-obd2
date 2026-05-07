# ADV350 OBD Logger — Project Context

> **Project**: Honda ADV350 OBD-II data logger & analyzer
> **Hardware**: ESP32-S3 N16R8 (DevKitC-1 compatible breakout)
> **Stack**: Rust + esp-idf-hal (std)
> **Backend**: Supabase (Postgres) — pipeline ส่งข้อมูลขึ้น cloud

---

## 🎯 Context for Claude Code

โปรเจกต์นี้เป็น embedded firmware project สำหรับติดตั้งบน Honda ADV350 (Euro 5) เพื่ออ่าน CAN bus ผ่าน OBD 6-pin connector แล้วส่งข้อมูล telemetry ขึ้น Supabase

**เมื่อช่วยเขียนโค้ด ให้คำนึงถึง:**

- **Resource constraints**: ESP32-S3 มี 16MB Flash + 8MB Octal PSRAM + 512KB SRAM — ใช้ memory อย่างประหยัด
- **No-std friendly**: แม้ใช้ esp-idf-hal (std) ก็พยายามหลีก allocation ใน hot path
- **Real-time CAN**: frame rate ของ CAN bus อาจสูงถึง 1000 frames/sec — buffer ต้อง lock-free หรือ ring buffer
- **Power loss safe**: รถมอเตอร์ไซค์ตัดไฟกระทันหัน — เขียน flash ต้อง atomic, ใช้ NVS commit pattern
- **Thermal**: รถจอดแดดไทย ambient ถึง 70°C — ใช้ algorithm ที่ CPU ต่ำ
- **OTA-first**: ทุก feature ต้องคิด OTA ตั้งแต่แรก — ห้ามมี state ที่ flash ใหม่แล้วพัง
- **อ่านอย่างเดียว** ในเฟสนี้ — **ห้ามเขียน CAN frame กลับเข้า ECU** จนกว่า reverse engineer สำเร็จและทดสอบบน bench

**Coding conventions:**
- Rust 2021 edition
- ใช้ `tracing` หรือ `log` + `esp-idf-svc` logger สำหรับ logging
- Error handling: ใช้ `anyhow::Result` ใน application code, `thiserror` สำหรับ library code
- Async: ใช้ `tokio` ผ่าน esp-idf-hal (std mode รองรับ)
- Comments: ภาษาอังกฤษสำหรับ technical comments, ไทยได้ถ้าอธิบาย business logic

---

## 📦 Hardware Inventory

### มีอยู่แล้ว ✅
- [x] ESP32-S3 core board N16R8 (16MB Flash + 8MB Octal PSRAM, DevKitC-1 compatible)
- [x] USB-C cable (data)

### Phase 1 — ต้องซื้อก่อนเริ่ม CAN integration
- [ ] SN65HVD230 CAN Transceiver module (~80 บาท)
- [ ] OBD 6-pin Honda Euro 5 → flying lead cable (~500 บาท)
- [ ] Breadboard + jumper wires (~200 บาท)
- [ ] Multimeter (~500–1,500 บาท)

### Phase 2 — สำหรับ deploy บนรถ
- [ ] DC-DC step-down 12V→5V/1A automotive grade (~80 บาท)
- [ ] Inline fuse holder + 1A fuse (~80 บาท)
- [ ] กล่องกันน้ำ IP65 (~150 บาท)
- [ ] สายไฟ + ขั้วต่อ automotive (~150 บาท)
- [ ] PCB prototype (~150 บาท)

### Phase 3 — Optional expansion
- [ ] SIM7600 4G + GNSS module via UART (~1,500 บาท)
- [ ] หรือแยก: u-blox NEO-M9N GPS (~800 บาท)

---

## 🏗️ Architecture Overview

```
┌─────────────────────────────────────────┐
│  Honda ADV350 (Euro 5)                  │
│  ECU → CAN Bus (500 kbps)               │
│       ↓                                  │
│  6-pin OBD Connector (ISO 19689)        │
│  Pin A=GND, B=CAN-H, E=CAN-L, F=+12V   │
└─────────────────────────────────────────┘
              ↓
┌─────────────────────────────────────────┐
│  SN65HVD230 CAN Transceiver             │
│  (3.3V differential CAN-H/CAN-L → TTL)  │
└─────────────────────────────────────────┘
              ↓
┌─────────────────────────────────────────┐
│  ESP32-S3 N16R8                         │
│  ├── TWAI controller (CAN driver)       │
│  ├── PSRAM ring buffer (1000+ frames)   │
│  ├── Frame decoder (Honda-specific IDs) │
│  ├── WiFi station (home/4G hotspot)     │
│  ├── BLE peripheral (mobile app)        │
│  ├── HTTPS client (Supabase REST)       │
│  ├── LittleFS (offline log buffer)      │
│  ├── NVS (config storage)               │
│  └── OTA updater (HTTPS)                │
└─────────────────────────────────────────┘
              ↓
┌─────────────────────────────────────────┐
│  Supabase (Postgres + Realtime)         │
│  Table: bike_telemetry                   │
│  ↓                                       │
│  Next.js Dashboard (real-time graphs)   │
└─────────────────────────────────────────┘
```

---

## 🛠️ Tech Stack Decisions

| Component | Choice | Rationale |
|---|---|---|
| MCU | ESP32-S3 N16R8 | TWAI ในตัว, BLE 5.0, WiFi, PSRAM 8MB, automotive-grade temp |
| Language | Rust + esp-idf-hal | Type safety, std support, มี ecosystem ที่ดี, port code ง่ายถ้าย้าย platform |
| Async runtime | tokio (via esp-idf) | Familiar, รองรับ HTTPS, BLE concurrent |
| CAN driver | esp-idf-svc TWAI | Native ESP-IDF support, hardware accelerated |
| Storage | LittleFS + NVS | Power-loss safe, wear leveling |
| Cloud | Supabase | Postgres, REST API ง่าย, free tier เพียงพอ, มี Realtime |
| Backend (future) | NestJS (existing Skilllane stack) | Reuse existing infrastructure |
| Dashboard | Next.js + Recharts | Familiar stack, real-time via Supabase subscription |

**ที่ไม่เลือกและทำไม:**
- ❌ Raspberry Pi 4/5 — ร้อนเกิน, SD corruption เสี่ยง, overkill
- ❌ Arduino IDE — ไม่ production-grade, debug ยาก
- ❌ MicroPython — performance ไม่พอสำหรับ CAN ความเร็วสูง
- ❌ ESP-IDF (C/C++) — Rust ปลอดภัยกว่าและ developer ถนัดกว่า

---

## 🚀 Development Roadmap

### Phase 0 — Toolchain Setup (วันแรก)
- [ ] ติดตั้ง Rust: `curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh`
- [ ] ติดตั้ง espup: `cargo install espup && espup install`
- [ ] ติดตั้ง flash tools: `cargo install cargo-espflash espflash`
- [ ] เพิ่ม `source ~/export-esp.sh` ใน `.zshrc`
- [ ] Generate template: `cargo generate esp-rs/esp-idf-template cargo` (เลือก esp32s3, std)
- [ ] Verify VS Code + rust-analyzer ทำงานได้

### Phase 1 — Hardware Bring-up
- [ ] เสียบ USB-C เข้ารู "USB" (native USB-Serial-JTAG)
- [ ] Verify device: `ls /dev/cu.usbmodem*`
- [ ] First flash: hello world + LED blink (GPIO 48)
- [ ] **Verify PSRAM 8MB**:
  ```rust
  let v: Vec<u8> = vec![0u8; 4 * 1024 * 1024];
  log::info!("PSRAM allocation OK: {} bytes", v.len());
  ```
- [ ] Verify Flash 16MB ใน partition table

### Phase 2 — Core Capabilities (สัปดาห์แรก)
- [ ] WiFi station mode + connect home WiFi
- [ ] HTTPS GET test (httpbin.org)
- [ ] HTTPS POST ไป Supabase test endpoint
- [ ] BLE peripheral basic (advertise + 1 read characteristic)
- [ ] **TWAI loopback test** (ไม่ต้องมี transceiver):
  ```rust
  let config = twai::config::Config::new(twai::TwaiMode::SelfTest)
      .set_bitrate(twai::BitRate::B500K);
  ```
- [ ] Send/receive dummy CAN frame in loopback

### Phase 3 — Software Architecture (สัปดาห์ที่ 2)
- [ ] NVS config storage (WiFi creds, Supabase URL/key)
- [ ] LittleFS partition + mount
- [ ] OTA partition table (`partitions.csv`)
- [ ] HTTPS OTA implementation
- [ ] Rollback on boot failure (3 strikes)
- [ ] Connection state machine
- [ ] Frame buffer (ring buffer in PSRAM, 1000+ frames)
- [ ] Batch HTTP POST to Supabase (100 frames/batch)
- [ ] Retry + exponential backoff

### Phase 4 — CAN Integration (เมื่อ transceiver มา)
- [ ] Wire SN65HVD230 → ESP32-S3 (GPIO 4 = TX, GPIO 5 = RX, 3.3V, GND)
- [ ] Bench test กับ ESP32 อีกตัวที่ส่ง CAN frame (หรือ CAN simulator)
- [ ] ทดสอบกับ ELM327 + OBD adapter ที่มี CAN simulation mode

### Phase 5 — Vehicle Integration (เมื่อ OBD cable + รถพร้อม)
- [ ] วัด pinout ที่ ADV350 ด้วย multimeter
- [ ] ทำสาย adapter (OBD 6-pin → CAN HAT + 12V → DC-DC)
- [ ] **READ-ONLY** capture session 30 นาที (ignition on, no start)
- [ ] **READ-ONLY** capture session 30 นาที (engine running, idle)
- [ ] **READ-ONLY** capture session 30 นาที (riding, varied throttle)
- [ ] Export logs → SavvyCAN / Wireshark for analysis
- [ ] Reverse-engineer Honda CAN IDs (RPM, speed, throttle, temp, gear)
- [ ] Implement decoder for known IDs

### Phase 6 — Production Deploy
- [ ] Mounting solution (vibration damping + airflow)
- [ ] กล่องกันน้ำ + heat dissipation
- [ ] Final wiring with fuse, automotive crimp connectors
- [ ] 24-hour soak test on bench
- [ ] 4-hour soak test in car under sun (verify thermal headroom)
- [ ] Monitor first week deployment closely

### Phase 7 — Backend & Dashboard
- [ ] Supabase schema (`bike_telemetry` table + indexes)
- [ ] RLS policies (device authentication)
- [ ] Next.js dashboard:
  - [ ] Real-time RPM/speed graph
  - [ ] Trip detection + history
  - [ ] Anomaly alerts (high temp, error codes)
  - [ ] Maintenance reminders
- [ ] (Optional) Mobile app via React Native + Supabase

---

## 📡 Honda ADV350 OBD Reference

### Connector: 6-pin ISO 19689 (Honda Euro 5 standard)

```
Pinout (looking into connector on bike):
┌─────────────┐
│  A   B   C  │
│  D   E   F  │
└─────────────┘

A = GND (chassis ground)
B = CAN-H
C = SCS (Service Check Signal — jumper to GND for diagnostic mode)
D = K-Line (Honda HDS / KWP2000 protocol)
E = CAN-L
F = +12V (battery, switched by ignition)
```

### Protocol
- **CAN bus**: ISO 11898, 500 kbps (Honda Euro 5 standard)
- **K-Line**: ISO 14230 (KWP2000), Honda HDS proprietary on top — **ไม่ใช้ใน project นี้**, focus CAN เท่านั้น

### Expected CAN IDs (ต้อง verify ด้วย capture จริง)
- ECU broadcasts ที่อาจเจอ: RPM, throttle position, coolant temp, vehicle speed, gear position, fuel level
- Honda ไม่เปิด DBC file → ต้อง reverse engineer

---

## 📁 Project Structure (Suggested)

```
adv350-logger/
├── CLAUDE.md                  # This file
├── README.md
├── .gitignore
├── firmware/                  # Main ESP32-S3 Rust firmware
│   ├── Cargo.toml
│   ├── rust-toolchain.toml    # esp channel
│   ├── sdkconfig.defaults     # ESP-IDF config (PSRAM, partitions)
│   ├── partitions.csv         # OTA partition layout
│   ├── build.rs
│   └── src/
│       ├── main.rs
│       ├── config.rs          # NVS config management
│       ├── wifi.rs            # WiFi connection + reconnect
│       ├── ble.rs             # BLE peripheral service
│       ├── can/
│       │   ├── mod.rs
│       │   ├── driver.rs      # TWAI wrapper
│       │   ├── decoder.rs     # Honda CAN ID decoder
│       │   └── buffer.rs      # Ring buffer in PSRAM
│       ├── cloud/
│       │   ├── mod.rs
│       │   ├── supabase.rs    # HTTPS client
│       │   └── batcher.rs     # Batch + retry logic
│       ├── storage/
│       │   ├── mod.rs
│       │   ├── nvs.rs
│       │   └── littlefs.rs    # Offline log buffer
│       ├── ota.rs             # OTA update handler
│       └── state.rs           # System state machine
├── tools/
│   ├── can-simulator/         # Rust util to send dummy frames for testing
│   └── log-analyzer/          # Parse captured logs
├── docs/
│   ├── can-ids.md             # Reverse-engineered Honda CAN IDs
│   ├── pinout.md              # Hardware wiring diagram
│   ├── deployment.md          # Mounting, power, troubleshooting
│   └── ota-process.md         # OTA workflow
├── backend/                   # NestJS API (future)
└── dashboard/                 # Next.js (future)
```

---

## ⚙️ Key Configuration Files

### `sdkconfig.defaults` (must-have)
```
# PSRAM (Octal, 8MB on N16R8)
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384

# Flash (16MB on N16R8)
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="16MB"

# Partition table (custom for OTA)
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"

# Compiler
CONFIG_COMPILER_OPTIMIZATION_PERF=y
CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_DISABLE=y

# Logging
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
```

### `partitions.csv` (16MB layout with OTA)
```
# Name,    Type, SubType,  Offset,   Size,     Flags
nvs,       data, nvs,      0x9000,   0x6000,
phy_init,  data, phy,      0xf000,   0x1000,
otadata,   data, ota,      0x10000,  0x2000,
app0,      app,  ota_0,    0x20000,  0x600000,
app1,      app,  ota_1,    0x620000, 0x600000,
storage,   data, littlefs, 0xC20000, 0x3E0000,
```

(app0 + app1 = 12MB for OTA, storage = ~4MB for offline logs)

---

## ⚠️ Critical Constraints & Gotchas

### Hardware
- **Octal PSRAM ≠ Quad PSRAM** — N16R8 ใช้ Octal, ต้อง config `CONFIG_SPIRAM_MODE_OCT=y` ไม่งั้น PSRAM ใช้ไม่ได้
- **GPIO 35, 36, 37** ใช้ไม่ได้บน N16R8 (ใช้สำหรับ Octal PSRAM internally)
- **ห้ามใช้ GPIO 0, 45, 46** สำหรับ peripheral (strapping pins)
- **TWAI default pins**: GPIO 4 (TX), GPIO 5 (RX) — ใช้ไม่ใช่ default ก็ได้ แต่ระวัง pin conflict

### CAN
- **อย่า write CAN frame** จนกว่าจะ reverse-engineer สำเร็จ + ทดสอบ bench — เสี่ยงทำให้ ECU เข้า limp mode
- **Bus-off recovery** — ถ้า bus error เยอะ ESP32 จะเข้า bus-off state → ต้อง implement recovery
- **Bitrate ต้องตรง** — Honda Euro 5 ใช้ 500 kbps; ผิด → bus error 100%

### Power
- ESP32-S3 ต้องการ 5V/500mA stable — DC-DC จากรถต้องมี filter ดี
- **Voltage spike จาก rail ของรถ** — ใส่ TVS diode + electrolytic cap กัน
- **Battery drain** — ถ้าจอดยาว ต้องมี deep sleep หรือ relay ตัดไฟ

### Software
- **NVS write จำกัด ~100K cycles** — อย่า write บ่อย
- **OTA partition switching** — boot ใหม่ครั้งแรกหลัง OTA ต้อง mark valid ภายใน N seconds ไม่งั้น rollback
- **HTTPS cert pinning** — ใช้ root cert ของ Let's Encrypt (Supabase ใช้)
- **Supabase rate limit** — free tier มี limit, ต้อง batch + throttle

---

## 🔧 Useful Commands

### Development
```bash
# Build
cargo build --release

# Flash + monitor
cargo run --release

# Monitor only
espflash monitor

# Flash specific binary
espflash flash --monitor target/xtensa-esp32s3-espidf/release/firmware

# Erase flash (factory reset)
espflash erase-flash

# Read flash size
espflash board-info
```

### Debugging
```bash
# Find device
ls /dev/cu.usbmodem*    # Native USB
ls /dev/cu.usbserial*   # UART bridge

# Manual reset to bootloader (if auto fails)
# Hold BOOT, press RESET, release BOOT

# Decode panic backtrace
espflash decode --target xtensa-esp32s3-espidf
```

### CAN Testing (Linux/Mac with USB-CAN adapter)
```bash
# Setup virtual CAN for testing
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0

# Send test frame
cansend vcan0 123#DEADBEEF

# Monitor
candump vcan0

# Replay capture
canplayer -I capture.log
```

---

## 📚 References

### Official Docs
- ESP32-S3 Datasheet: https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf
- ESP-IDF Programming Guide: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/
- esp-rs Book: https://esp-rs.github.io/book/
- esp-idf-hal docs: https://docs.esp-rs.org/esp-idf-hal/

### Honda ADV350
- Service Manual (need to obtain — for OBD pinout confirmation)
- Honda HISS / Smart Key info: skip in v1, read-only doesn't need bypass

### CAN Reverse Engineering
- SavvyCAN: https://www.savvycan.com/
- python-can: https://python-can.readthedocs.io/
- HondaECU project: https://github.com/RyanHope/HondaECU (K-Line reference)

### Supabase Integration
- REST API: https://supabase.com/docs/reference/api
- Realtime: https://supabase.com/docs/guides/realtime

---

## 🔒 Security Notes

- **Service role key** เก็บใน NVS encrypted partition (ไม่ใช่ commit ใน git)
- **Device authentication** — ใช้ device-specific JWT หรือ API key
- **OTA binary signing** — ใช้ secure boot + signed app images (production)
- **BLE pairing** — ใช้ LE Secure Connections + bonding (ไม่ใช่ Just Works)

---

## 🎯 Definition of Done (v1.0)

- [ ] อ่าน RPM, vehicle speed, throttle position, coolant temp ได้แม่นยำ
- [ ] Stream ข้อมูลขึ้น Supabase แบบ real-time (latency < 5 sec)
- [ ] Buffer ข้อมูลตอน offline แล้ว flush เมื่อกลับ online
- [ ] OTA update ทำงานได้ end-to-end
- [ ] รัน 7 วันต่อเนื่องบนรถจริงไม่ crash
- [ ] Dashboard แสดงข้อมูล trip ย้อนหลังได้
- [ ] BLE pair กับ mobile app และ control basic ได้

---

*Last updated: 2026-05-07*
*Maintainer: MEGALODON*