import 'package:sqflite/sqflite.dart';
import 'package:path/path.dart';
import '../models/telemetry.dart';

class DatabaseService {
  static Database? _db;

  Future<Database> get database async {
    _db ??= await _initDb();
    return _db!;
  }

  Future<Database> _initDb() async {
    final path = join(await getDatabasesPath(), 'adv350.db');
    return openDatabase(
      path,
      version: 6,
      onCreate: (db, version) async {
        await db.execute('''
          CREATE TABLE telemetry (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            rpm INTEGER NOT NULL,
            speed INTEGER NOT NULL,
            throttle INTEGER NOT NULL,
            coolant_temp INTEGER NOT NULL,
            raw_gear INTEGER NOT NULL DEFAULT 0,
            raw_fuel INTEGER NOT NULL DEFAULT 0,
            map_kpa INTEGER NOT NULL DEFAULT 0,
            iat INTEGER NOT NULL DEFAULT 0,
            engine_load INTEGER NOT NULL DEFAULT 0,
            ignition_timing INTEGER NOT NULL DEFAULT 0,
            raw_ble_hex TEXT NOT NULL DEFAULT '',
            fuel_rate_lph REAL NOT NULL DEFAULT 0,
            cvt_ratio REAL NOT NULL DEFAULT 0,
            riding_score INTEGER NOT NULL DEFAULT 0,
            timestamp INTEGER NOT NULL,
            synced INTEGER NOT NULL DEFAULT 0
          )
        ''');
      },
      onUpgrade: (db, oldVersion, newVersion) async {
        if (oldVersion < 2) {
          await db.execute('ALTER TABLE telemetry ADD COLUMN map_kpa INTEGER NOT NULL DEFAULT 0');
          await db.execute('ALTER TABLE telemetry ADD COLUMN iat INTEGER NOT NULL DEFAULT 0');
          await db.execute('ALTER TABLE telemetry ADD COLUMN engine_load INTEGER NOT NULL DEFAULT 0');
          await db.execute('ALTER TABLE telemetry ADD COLUMN ignition_timing INTEGER NOT NULL DEFAULT 0');
          await db.execute('ALTER TABLE telemetry RENAME COLUMN gear TO raw_gear');
          await db.execute('ALTER TABLE telemetry RENAME COLUMN fuel_level TO raw_fuel');
        }
        if (oldVersion < 3) {
          await db.execute("ALTER TABLE telemetry ADD COLUMN raw_ble_hex TEXT NOT NULL DEFAULT ''");
        }
        if (oldVersion < 4) {
          // Recreate table with DEFAULT 0 on raw_gear/raw_fuel (SQLite can't ALTER DEFAULT)
          await db.execute('''
            CREATE TABLE telemetry_new (
              id INTEGER PRIMARY KEY AUTOINCREMENT,
              rpm INTEGER NOT NULL,
              speed INTEGER NOT NULL,
              throttle INTEGER NOT NULL,
              coolant_temp INTEGER NOT NULL,
              raw_gear INTEGER NOT NULL DEFAULT 0,
              raw_fuel INTEGER NOT NULL DEFAULT 0,
              map_kpa INTEGER NOT NULL DEFAULT 0,
              iat INTEGER NOT NULL DEFAULT 0,
              engine_load INTEGER NOT NULL DEFAULT 0,
              ignition_timing INTEGER NOT NULL DEFAULT 0,
              raw_ble_hex TEXT NOT NULL DEFAULT '',
              timestamp INTEGER NOT NULL,
              synced INTEGER NOT NULL DEFAULT 0
            )
          ''');
          await db.execute('''
            INSERT INTO telemetry_new (id, rpm, speed, throttle, coolant_temp, raw_gear, raw_fuel, map_kpa, iat, engine_load, ignition_timing, raw_ble_hex, timestamp, synced)
            SELECT id, rpm, speed, throttle, coolant_temp, raw_gear, raw_fuel, map_kpa, iat, engine_load, ignition_timing, raw_ble_hex, timestamp, synced FROM telemetry
          ''');
          await db.execute('DROP TABLE telemetry');
          await db.execute('ALTER TABLE telemetry_new RENAME TO telemetry');
        }
        if (oldVersion < 5) {
          await db.execute('ALTER TABLE telemetry ADD COLUMN fuel_rate_lph REAL NOT NULL DEFAULT 0');
          await db.execute('ALTER TABLE telemetry ADD COLUMN cvt_ratio REAL NOT NULL DEFAULT 0');
          await db.execute('ALTER TABLE telemetry ADD COLUMN riding_score INTEGER NOT NULL DEFAULT 0');
          // Backfill from raw_ble_hex (16-byte vehicle_data packet)
          // bytes 11-12 = fuel_rate*100, 13-14 = cvt*100, 15 = score
          final rows = await db.query('telemetry',
              columns: ['id', 'raw_ble_hex'],
              where: "raw_ble_hex != '' AND length(raw_ble_hex) >= 32");
          final batch = db.batch();
          for (final row in rows) {
            final hex = row['raw_ble_hex'] as String;
            try {
              final fuelLo = int.parse(hex.substring(22, 24), radix: 16);
              final fuelHi = int.parse(hex.substring(24, 26), radix: 16);
              final cvtLo = int.parse(hex.substring(26, 28), radix: 16);
              final cvtHi = int.parse(hex.substring(28, 30), radix: 16);
              final score = int.parse(hex.substring(30, 32), radix: 16);
              final fuelRate = (fuelLo | (fuelHi << 8)) / 100.0;
              final cvtRatio = (cvtLo | (cvtHi << 8)) / 100.0;
              batch.update('telemetry', {
                'fuel_rate_lph': fuelRate,
                'cvt_ratio': cvtRatio,
                'riding_score': score,
              }, where: 'id = ?', whereArgs: [row['id']]);
            } catch (_) {}
          }
          await batch.commit(noResult: true);
        }
        if (oldVersion < 6) {
          // Re-run backfill for DBs that hit v5 without backfill
          final rows = await db.query('telemetry',
              columns: ['id', 'raw_ble_hex'],
              where: "raw_ble_hex != '' AND length(raw_ble_hex) >= 32 AND fuel_rate_lph = 0");
          final batch = db.batch();
          for (final row in rows) {
            final hex = row['raw_ble_hex'] as String;
            try {
              final fuelLo = int.parse(hex.substring(22, 24), radix: 16);
              final fuelHi = int.parse(hex.substring(24, 26), radix: 16);
              final cvtLo = int.parse(hex.substring(26, 28), radix: 16);
              final cvtHi = int.parse(hex.substring(28, 30), radix: 16);
              final score = int.parse(hex.substring(30, 32), radix: 16);
              final fuelRate = (fuelLo | (fuelHi << 8)) / 100.0;
              final cvtRatio = (cvtLo | (cvtHi << 8)) / 100.0;
              if (fuelRate > 0 || cvtRatio > 0 || score > 0) {
                batch.update('telemetry', {
                  'fuel_rate_lph': fuelRate,
                  'cvt_ratio': cvtRatio,
                  'riding_score': score,
                }, where: 'id = ?', whereArgs: [row['id']]);
              }
            } catch (_) {}
          }
          await batch.commit(noResult: true);
        }
      },
    );
  }

  Future<int> insertTelemetry(Telemetry t) async {
    final db = await database;
    return db.insert('telemetry', t.toMap());
  }

  Future<List<Telemetry>> getUnsynced({int limit = 100}) async {
    final db = await database;
    final maps = await db.query(
      'telemetry',
      where: 'synced = 0',
      orderBy: 'timestamp ASC',
      limit: limit,
    );
    return maps.map(Telemetry.fromMap).toList();
  }

  Future<void> markSynced(List<int> ids) async {
    final db = await database;
    final batch = db.batch();
    for (final id in ids) {
      batch.update('telemetry', {'synced': 1},
          where: 'id = ?', whereArgs: [id]);
    }
    await batch.commit(noResult: true);
  }

  Future<List<Telemetry>> getRecent({int limit = 100}) async {
    final db = await database;
    final maps = await db.query(
      'telemetry',
      orderBy: 'timestamp DESC',
      limit: limit,
    );
    return maps.map(Telemetry.fromMap).toList();
  }

  Future<int> getUnsyncedCount() async {
    final db = await database;
    final result =
        await db.rawQuery('SELECT COUNT(*) as cnt FROM telemetry WHERE synced = 0');
    return result.first['cnt'] as int;
  }
}
