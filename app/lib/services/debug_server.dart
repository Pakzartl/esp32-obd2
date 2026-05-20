import 'dart:convert';
import 'dart:io';
import 'package:path_provider/path_provider.dart';
import 'database_service.dart';

class DebugServer {
  static final DebugServer _instance = DebugServer._();
  factory DebugServer() => _instance;
  DebugServer._();

  HttpServer? _server;
  final _db = DatabaseService();
  static const int port = 8350;

  bool get isRunning => _server != null;

  Future<void> start() async {
    if (_server != null) return;
    _server = await HttpServer.bind(InternetAddress.loopbackIPv4, port);
    _server!.listen(_handleRequest);
  }

  Future<void> stop() async {
    await _server?.close();
    _server = null;
  }

  Future<void> _handleRequest(HttpRequest req) async {
    final path = req.uri.path;
    try {
      switch (path) {
        case '/':
          _json(req, {'status': 'ok', 'endpoints': ['/db/count', '/db/recent', '/db/schema', '/db/export', '/db/sql', '/raw/list', '/raw/pull']});
        case '/db/count':
          await _dbCount(req);
        case '/db/recent':
          await _dbRecent(req);
        case '/db/schema':
          await _dbSchema(req);
        case '/db/export':
          await _dbExport(req);
        case '/db/sql':
          await _dbSql(req);
        case '/raw/list':
          await _rawList(req);
        default:
          if (path.startsWith('/raw/pull/')) {
            await _rawPull(req, path.substring('/raw/pull/'.length));
          } else {
            _json(req, {'error': 'not found'}, status: 404);
          }
      }
    } catch (e) {
      _json(req, {'error': e.toString()}, status: 500);
    }
  }

  Future<void> _dbCount(HttpRequest req) async {
    final db = await _db.database;
    final total = await db.rawQuery('SELECT COUNT(*) as cnt FROM telemetry');
    final unsynced = await db.rawQuery('SELECT COUNT(*) as cnt FROM telemetry WHERE synced = 0');
    final newest = await db.rawQuery('SELECT MAX(timestamp) as ts FROM telemetry');
    final oldest = await db.rawQuery('SELECT MIN(timestamp) as ts FROM telemetry');
    _json(req, {
      'total': total.first['cnt'],
      'unsynced': unsynced.first['cnt'],
      'newest_ts': newest.first['ts'],
      'oldest_ts': oldest.first['ts'],
    });
  }

  Future<void> _dbRecent(HttpRequest req) async {
    final limit = int.tryParse(req.uri.queryParameters['limit'] ?? '20') ?? 20;
    final db = await _db.database;
    final rows = await db.query('telemetry', orderBy: 'id DESC', limit: limit);
    _json(req, {'count': rows.length, 'rows': rows});
  }

  Future<void> _dbSchema(HttpRequest req) async {
    final db = await _db.database;
    final result = await db.rawQuery("SELECT sql FROM sqlite_master WHERE type='table' AND name='telemetry'");
    _json(req, {'schema': result.first['sql']});
  }

  Future<void> _dbExport(HttpRequest req) async {
    final db = await _db.database;
    final file = File(db.path);
    if (!await file.exists()) {
      _json(req, {'error': 'db file not found'}, status: 404);
      return;
    }
    req.response.headers.contentType = ContentType.binary;
    req.response.headers.set('Content-Disposition', 'attachment; filename="adv350.db"');
    await file.openRead().pipe(req.response);
  }

  Future<void> _dbSql(HttpRequest req) async {
    final db = await _db.database;
    String? sql;

    if (req.method == 'POST') {
      final body = await utf8.decoder.bind(req).join();
      try {
        final parsed = jsonDecode(body) as Map<String, dynamic>;
        sql = parsed['sql'] as String?;
      } catch (_) {
        sql = body.trim();
      }
    } else {
      sql = req.uri.queryParameters['q'];
    }

    if (sql == null || sql.isEmpty) {
      _json(req, {
        'error': 'no sql provided',
        'usage_get': 'GET /db/sql?q=SELECT+*+FROM+telemetry+LIMIT+5',
        'usage_post': 'POST /db/sql  body: {"sql":"SELECT * FROM telemetry LIMIT 5"}',
      }, status: 400);
      return;
    }

    final upper = sql.trimLeft().toUpperCase();
    if (upper.startsWith('SELECT') || upper.startsWith('PRAGMA') || upper.startsWith('EXPLAIN')) {
      final rows = await db.rawQuery(sql);
      _json(req, {'sql': sql, 'count': rows.length, 'rows': rows});
    } else {
      final affected = await db.rawUpdate(sql);
      _json(req, {'sql': sql, 'affected': affected});
    }
  }

  Future<void> _rawList(HttpRequest req) async {
    final dir = await getApplicationDocumentsDirectory();
    final backupDir = Directory('${dir.path}/raw_backup');
    if (!await backupDir.exists()) {
      _json(req, {'files': []});
      return;
    }
    final files = await backupDir.list().toList();
    files.sort((a, b) => b.path.compareTo(a.path));
    final list = <Map<String, dynamic>>[];
    for (final f in files) {
      final stat = await f.stat();
      list.add({
        'name': f.path.split('/').last,
        'size': stat.size,
        'modified': stat.modified.toIso8601String(),
      });
    }
    _json(req, {'files': list});
  }

  Future<void> _rawPull(HttpRequest req, String filename) async {
    final dir = await getApplicationDocumentsDirectory();
    final file = File('${dir.path}/raw_backup/$filename');
    if (!await file.exists()) {
      _json(req, {'error': 'file not found'}, status: 404);
      return;
    }
    req.response.headers.contentType = ContentType.text;
    req.response.headers.set('Content-Disposition', 'attachment; filename="$filename"');
    await file.openRead().pipe(req.response);
  }

  void _json(HttpRequest req, Map<String, dynamic> data, {int status = 200}) {
    req.response.statusCode = status;
    req.response.headers.contentType = ContentType.json;
    req.response.write(jsonEncode(data));
    req.response.close();
  }
}
