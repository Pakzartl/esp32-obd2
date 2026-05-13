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
      version: 1,
      onCreate: (db, version) async {
        await db.execute('''
          CREATE TABLE telemetry (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            rpm INTEGER NOT NULL,
            speed INTEGER NOT NULL,
            throttle INTEGER NOT NULL,
            coolant_temp INTEGER NOT NULL,
            gear INTEGER NOT NULL,
            fuel_level INTEGER NOT NULL,
            timestamp INTEGER NOT NULL,
            synced INTEGER NOT NULL DEFAULT 0
          )
        ''');
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
