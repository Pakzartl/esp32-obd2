# Honda ADV350 CAN Bus — Reverse Engineering Results

**Date**: 2026-05-19
**Vehicle**: Honda ADV350 (Euro 5, 2021+)
**ECU**: Keihin/Hitachi Astemo PGM-FI (RH850-based)
**Board**: ArtronShop ESP-OBD2 (ESP32-WROOM-32E + SN65HVD230)
**Adapter**: 6-pin ISO 19689 → OBD2 16-pin (from Shopee)

---

## Protocol Discovery

### What does NOT work

| Protocol | CAN ID | Type | Result |
|----------|--------|------|--------|
| Standard OBD2 Mode 01 | 0x7DF (11-bit) | Functional broadcast | **NO RESPONSE** — ECU ignores |
| Standard OBD2 Physical | 0x7E0 (11-bit) | Physical ECU address | **NO RESPONSE** |
| Passive CAN sniffing | Listen-only | Any bitrate | **ZERO frames** — ECU does not broadcast |

### What WORKS

| Protocol | Request CAN ID | Response CAN ID | Type |
|----------|---------------|-----------------|------|
| **Honda UDS 29-bit** | 0x18DA10F1 | 0x18DAF110 | Physical addressing |
| **Honda HDS 29-bit** | 0x18DB33F1 | 0x18DAF110 | Functional broadcast |

**Addressing scheme** (ISO 14229 UDS over ISO 15765-2):
- `0x18DA` = ISO-TP header prefix
- `10` = ECU target address (main engine ECU)
- `F1` = Tester source address
- Response: ECU(`10`) → Tester(`F1`)

### CAN Bus Configuration
- **Bitrate**: 500 kbps (confirmed — 250k/125k/1M produce massive bus errors)
- **Frame format**: Extended 29-bit CAN IDs only
- **No passive broadcast**: ECU only responds to UDS requests, never broadcasts unsolicited
- **ISO-TP**: Single-frame only confirmed (PCI byte 0x0N where N=payload length)

---

## Confirmed UDS DIDs (Data Identifiers)

### From DID Brute-Force Scan (0xF400-0xF4FF)

Honda maps standard OBD2 PIDs into UDS DID space as `0xF4xx` where `xx` = OBD2 PID number.

| DID | OBD2 PID | Parameter | Bytes | Formula | Unit | Confirmed |
|-----|----------|-----------|-------|---------|------|-----------|
| 0xF402 | 0x02 | Freeze frame DTC | 2 | (A<<8)\|B | DTC code | scan |
| 0xF404 | 0x04 | Engine load | 1 | A*100/255 | % | scan+poll |
| 0xF405 | 0x05 | Coolant temperature | 1 | A-40 | °C | poll |
| 0xF406 | 0x06 | Short-term fuel trim | 2 | (A-128)*100/128 | % | scan |
| 0xF40B | 0x0B | MAP (intake pressure) | 1 | A | kPa | poll |
| 0xF40C | 0x0C | Engine RPM | 2 | (A*256+B)/4 | RPM | scan+poll |
| 0xF40D | 0x0D | Vehicle speed | 1 | A | km/h | poll |
| 0xF40E | 0x0E | Ignition timing advance | 1 | (A/2)-64 | °BTDC | scan |
| 0xF40F | 0x0F | Intake air temperature | 1 | A-40 | °C | poll |
| 0xF411 | 0x11 | Throttle position | 1 | A*100/255 | % | poll |
| 0xF412 | 0x12 | Secondary air status | 1 | bitmask | - | scan |
| 0xF41C | 0x1C | OBD standards compliance | 1 | 0x24 | - | scan |

**OBD compliance byte 0x24 decodes as**: ISO 15765-4 CAN + ISO 14230-4 KWP2000

### From UDS Identification Scan (0xF100-0xF1FF)

| DID | Parameter | Bytes | Value | Notes |
|-----|-----------|-------|-------|-------|
| 0xF186 | Active diagnostic session | 1 | 0x01=default, 0x03=extended | Confirms session control works |
| 0xF18A | System supplier identifier | 1 | 0x01 | Intermittent — may need specific timing |

### DIDs NOT Supported (NRC 0x31 requestOutOfRange)

These DID ranges returned no results after full brute-force scan:

| Range | Description | Result |
|-------|-------------|--------|
| 0x0000-0x00FF | Honda proprietary (legacy) | 0 DIDs |
| 0x0100-0x01FF | Honda proprietary | 0 DIDs |
| 0x0200-0x02FF | Honda proprietary | 0 DIDs |
| 0x1000-0x10FF | Honda extended | 0 DIDs |
| 0x2000-0x20FF | Honda extended | 0 DIDs |
| 0x2100-0x21FF | Honda extended | 0 DIDs |
| 0x3000-0x30FF | Honda calibration | 0 DIDs |
| 0x4000-0x40FF | Honda calibration | 0 DIDs |
| 0x6000-0x60FF | Honda calibration | 0 DIDs |
| 0xD000-0xD0FF | Honda proprietary (Keihin) | 0 DIDs |
| 0xE000-0xE0FF | Honda Euro 5 proprietary | 0 DIDs |

### Not Available from ECU

| Parameter | Tried DIDs | Status |
|-----------|-----------|--------|
| Battery voltage | 0xF442, 0xD001, 0xE001 + full range scans | Not exposed |
| Fuel rate | 0xF45E + full range scans | Not exposed |
| Fuel level | All ranges | Not exposed |
| Injector pulse width | All ranges | Not exposed |
| Lambda/O2 sensor | 0xF414, 0xF415 + ranges | Not exposed |
| Gear position | All ranges | N/A (CVT, no gears) |

---

## UDS Services Confirmed

| Service ID | Name | Status |
|------------|------|--------|
| 0x10 | DiagnosticSessionControl | Works (default=0x01, extended=0x03) |
| 0x22 | ReadDataByIdentifier | Works (primary data access method) |
| 0x3E | TesterPresent | Works (keep-alive, response 0x7E 0x00) |

### Negative Response Codes Observed

| NRC | Name | When |
|-----|------|------|
| 0x31 | requestOutOfRange | DID not supported by ECU |
| 0x22 | conditionsNotCorrect | DID exists but needs different session (rare) |

---

## Diagnostic Session Behavior

- **Default session (0x01)**: All F4xx DIDs accessible, no timeout
- **Extended session (0x03)**: Same DIDs plus F186. Some F4xx DIDs intermittently unavailable (scan timing issue, not real restriction)
- **Tester present**: Required every ~5 seconds to keep session alive

---

## CAN Bus Physical Layer

- **Connector**: Honda 6-pin ISO 19689 → OBD2 16-pin adapter
- **Pin mapping**: Honda B(CAN-H)→OBD2 pin 6, Honda E(CAN-L)→OBD2 pin 14
- **Transceiver**: SN65HVD230DR on ESP-OBD2 board (GPIO26=TX, GPIO27=RX)
- **Termination**: JP1 solder bridge (120Ω) — leave ON for this setup, bus is short
- **Bus topology**: Point-to-point (ESP32 ↔ ECU only, no other nodes)

---

## Key Findings

1. **Honda ADV350 does NOT use standard OBD2** — ignores 11-bit CAN IDs entirely
2. **29-bit extended CAN with UDS** is the correct protocol
3. **ECU never broadcasts** — all data must be requested via ReadDataByIdentifier (0x22)
4. **DID format is 0xF4xx** — maps directly to standard OBD2 PID numbers
5. **Only first PID block supported** — PIDs 0x01-0x1F mapped to 0xF401-0xF41F; PIDs 0x20+ (battery, fuel rate) not available
6. **No Honda proprietary DIDs found** — D0xx, E0xx ranges are empty, unlike Honda car ECUs
7. **Fuel consumption calculable** from MAP + RPM + IAT (Speed-Density formula) without needing direct fuel rate DID
8. **Battery voltage not available** from ECU — would need external ADC measurement

---

## Bitrate Scan Evidence

| Bitrate | BUSerr (with TX) | BUSerr (listen-only) | Conclusion |
|---------|-------------------|----------------------|------------|
| 125 kbps | not tested | 0 | Wrong |
| 250 kbps | 80,035 | 0 | Wrong (massive errors) |
| 500 kbps | 481 | 0 | **Correct** (errors from no-ACK TX only) |
| 1000 kbps | not tested | 0 | Wrong |

---

## Derived Metrics (calculable from confirmed DIDs)

| Metric | Input DIDs | Formula |
|--------|-----------|---------|
| Fuel consumption (km/L) | F40B(MAP) + F40C(RPM) + F40F(IAT) | Speed-Density |
| Acceleration (m/s²) | F40D(speed) | Savitzky-Golay derivative |
| G-force | F40D(speed) | accel / 9.81 |
| Wheel power (kW) | F40D(speed) + accel + drag model | (Ma + ½ρCdAv² + CrMg)·v |
| CVT ratio | F40C(RPM) + F40D(speed) | RPM / (v × 60 / circumference) |
| Braking distance | F40D(speed) | ∫v(t)dt during deceleration |
| Riding score | F40D + F40C + F411(throttle) | Smoothness metrics |

---

## Hardware Setup

```
Honda ADV350 ECU
    │
    ├── 6-pin ISO 19689 OBD connector
    │   Pin A = GND
    │   Pin B = CAN-H ──┐
    │   Pin C = SCS      │
    │   Pin D = K-Line   │  6-to-16 adapter cable
    │   Pin E = CAN-L ──┤
    │   Pin F = +12V     │
    │                    │
    └────────────────────┘
                         │
    ┌────────────────────┘
    │
    OBD2 16-pin J1962 (on ESP-OBD2 board)
    │   Pin 4/5 = GND
    │   Pin 6 = CAN-H → SN65HVD230 pin 7
    │   Pin 14 = CAN-L → SN65HVD230 pin 6
    │   Pin 16 = +12V → TPS54202 → 3.3V
    │
    ESP32-WROOM-32E
        GPIO26 = CAN TX (SN65HVD230 DI)
        GPIO27 = CAN RX (SN65HVD230 RO)
```

---

## Firmware Architecture

```
can_sniffer_task (Core 1, priority 5)
    └── twai_receive() → obd2_process_frame() → parse UDS response → update vehicle_data

obd2_poll_task (Core 1, priority 4)
    └── cycle through DIDs: F40C→F40D→F411→F405→F404→F40B→F40F
        + tester_present every 3s

metrics_task (Core 1, priority 4)
    └── 10Hz: calc fuel, accel, g-force, power, riding score

can_diag_task (one-shot at boot)
    └── self-test → bitrate scan → protocol probe (11-bit + 29-bit)

did_scan_task (on-demand via /api/scan)
    └── extended session → brute-force 256 DIDs → report results
```

---

## Timeline of Discovery

1. **LISTEN_ONLY mode** → 0 frames, 0 errors → bus appears dead
2. **NORMAL mode + standard OBD2 (0x7DF, 11-bit)** → TXerr:105, BUSerr:481, no response → ECU ignores 11-bit
3. **250 kbps test** → BUSerr:80,035 → confirmed 500 kbps correct
4. **NORMAL mode + no TX** → 0 everything → ECU does not broadcast
5. **CAN diagnostic probe** → tried 5 protocols, **Honda 29-bit (0x18DA10F1) got response!**
6. **UDS polling** → ReadDataByIdentifier (0x22) with 0xF4xx DIDs → **real sensor data flowing**
7. **DID brute-force scan** → 14 confirmed DIDs, battery/fuel not available from ECU
