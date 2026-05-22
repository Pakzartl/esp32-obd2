CREATE TABLE IF NOT EXISTS telemetry (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  device_id TEXT NOT NULL DEFAULT 'adv350-01',
  rpm REAL,
  speed REAL,
  throttle REAL,
  coolant_temp REAL,
  map_kpa REAL,
  iat REAL,
  engine_load REAL,
  ignition_timing REAL,
  raw_ble_hex TEXT,
  recorded_at TEXT NOT NULL,
  received_at TEXT NOT NULL DEFAULT (datetime('now')),
  trip_id TEXT
);

CREATE INDEX IF NOT EXISTS idx_telemetry_recorded ON telemetry(recorded_at);
CREATE INDEX IF NOT EXISTS idx_telemetry_device ON telemetry(device_id);
CREATE INDEX IF NOT EXISTS idx_telemetry_trip ON telemetry(trip_id);

CREATE TABLE IF NOT EXISTS firmware (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  component TEXT NOT NULL DEFAULT 'firmware-s3',
  version TEXT NOT NULL,
  changelog TEXT NOT NULL DEFAULT '',
  download_url TEXT NOT NULL,
  size INTEGER NOT NULL DEFAULT 0,
  created_at TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX IF NOT EXISTS idx_firmware_component ON firmware(component);
