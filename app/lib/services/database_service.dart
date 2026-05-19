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
      version: 3,
      onCreate: (db, version) async {
        await db.execute('''
          CREATE TABLE telemetry (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            rpm INTEGER NOT NULL,
            speed INTEGER NOT NULL,
            throttle INTEGER NOT NULL,
            coolant_temp INTEGER NOT NULL,
            map_kpa INTEGER NOT NULL DEFAULT 0,
            iat INTEGER NOT NULL DEFAULT 0,
            engine_load INTEGER NOT NULL DEFAULT 0,
            ignition_timing INTEGER NOT NULL DEFAULT 0,
            raw_ble_hex TEXT NOT NULL DEFAULT '',
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
