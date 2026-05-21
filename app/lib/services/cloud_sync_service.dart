import 'dart:async';
import 'dart:convert';
import 'package:http/http.dart' as http;
import 'database_service.dart';

class CloudSyncService {
  static const String _apiBase = 'https://adv350.pakzartl.xyz';
  static const int _batchSize = 100;
  static const Duration _interval = Duration(seconds: 30);

  final DatabaseService _db;
  String _apiKey = '';
  Timer? _timer;
  bool _syncing = false;
  int _lastSyncedCount = 0;
  String? _lastError;

  CloudSyncService(this._db);

  int get lastSyncedCount => _lastSyncedCount;
  String? get lastError => _lastError;
  bool get isRunning => _timer != null;

  void configure({required String apiKey}) {
    _apiKey = apiKey;
  }

  void startPeriodic() {
    _timer?.cancel();
    _timer = Timer.periodic(_interval, (_) => syncOnce());
    syncOnce();
  }

  void stop() {
    _timer?.cancel();
    _timer = null;
  }

  Future<int> syncOnce() async {
    if (_syncing || _apiKey.isEmpty) return 0;
    _syncing = true;
    _lastError = null;
    int totalSynced = 0;

    try {
      while (true) {
        final rows = await _db.getUnsynced(limit: _batchSize);
        if (rows.isEmpty) break;

        final payload = {
          'device_id': 'adv350-01',
          'rows': rows.map((t) => {
            'rpm': t.rpm,
            'speed': t.speed,
            'throttle': t.throttle,
            'coolant_temp': t.coolantTemp,
            'map_kpa': t.mapKpa,
            'iat': t.iat,
            'engine_load': t.engineLoad,
            'ignition_timing': t.ignitionTiming,
            'raw_ble_hex': t.rawBleHex,
            'recorded_at': t.timestamp.toUtc().toIso8601String(),
          }).toList(),
        };

        final res = await http.post(
          Uri.parse('$_apiBase/api/telemetry'),
          headers: {
            'Content-Type': 'application/json',
            'X-API-Key': _apiKey,
          },
          body: jsonEncode(payload),
        ).timeout(const Duration(seconds: 15));

        if (res.statusCode == 200) {
          final ids = rows.where((t) => t.id != null).map((t) => t.id!).toList();
          await _db.markSynced(ids);
          totalSynced += ids.length;
        } else {
          _lastError = 'HTTP ${res.statusCode}: ${res.body}';
          break;
        }
      }
    } catch (e) {
      _lastError = e.toString();
    } finally {
      _syncing = false;
      _lastSyncedCount += totalSynced;
    }
    return totalSynced;
  }

  void dispose() {
    stop();
  }
}
