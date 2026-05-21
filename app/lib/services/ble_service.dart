import 'dart:async';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../models/telemetry.dart';

class BleService {
  static const String canServiceUuid =
      '12345678-1234-5678-1234-56789abcdef0';
  static const String canFrameCharUuid =
      '12345678-1234-5678-1234-56789abcdef1';
  static const String vehicleDataCharUuid =
      '12345678-1234-5678-1234-56789abcdef3';
  static const String canCtrlCharUuid =
      '12345678-1234-5678-1234-56789abcdef2';
  static const String otaDataCharUuid =
      '12345678-1234-5678-1234-56789abcdef6';
  static const String fwVersionCharUuid =
      '12345678-1234-5678-1234-56789abcdef7';
  static const String mgmtCharUuid =
      '12345678-1234-5678-1234-56789abcdef9';
  static const String deviceName = 'ADV350-R';

  BluetoothDevice? _device;
  BluetoothCharacteristic? _ctrlChar;
  BluetoothCharacteristic? _otaDataChar;
  BluetoothCharacteristic? _fwVersionChar;
  BluetoothCharacteristic? _mgmtChar;
  StreamSubscription? _subscription;
  StreamSubscription? _connStateSub;
  bool _autoReconnect = true;
  final _telemetryController = StreamController<Telemetry>.broadcast();
  final _rawDataController = StreamController<List<int>>.broadcast();
  final _connectionController = StreamController<bool>.broadcast();

  static const String _lastDeviceKey = 'last_ble_device_id';

  Stream<Telemetry> get telemetryStream => _telemetryController.stream;
  Stream<List<int>> get rawDataStream => _rawDataController.stream;
  Stream<bool> get connectionStream => _connectionController.stream;
  bool get isConnected => _device != null;

  Future<String?> get lastDeviceId async {
    final prefs = await SharedPreferences.getInstance();
    return prefs.getString(_lastDeviceKey);
  }

  Future<void> _saveDevice(String remoteId) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_lastDeviceKey, remoteId);
  }

  Future<void> clearLastDevice() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.remove(_lastDeviceKey);
  }

  Future<List<ScanResult>> scan(
      {Duration timeout = const Duration(seconds: 5)}) async {
    final results = <ScanResult>[];

    final sub = FlutterBluePlus.scanResults.listen((r) {
      for (final result in r) {
        final name = result.advertisementData.advName.isNotEmpty
            ? result.advertisementData.advName
            : result.device.platformName;
        if (name.startsWith('ADV350') &&
            !results
                .any((e) => e.device.remoteId == result.device.remoteId)) {
          results.add(result);
        }
      }
    });

    await FlutterBluePlus.startScan(timeout: timeout);
    await Future.delayed(timeout);
    await sub.cancel();
    return results;
  }

  Future<void> connect(BluetoothDevice device) async {
    await device.connect(
      license: License.free,
      autoConnect: false,
      mtu: 23,
      timeout: const Duration(seconds: 10),
    );
    _device = device;
    _autoReconnect = true;
    _connectionController.add(true);
    _saveDevice(device.remoteId.str);

    _connStateSub?.cancel();
    _connStateSub = device.connectionState.listen((state) {
      if (state == BluetoothConnectionState.disconnected) {
        _device = null;
        _ctrlChar = null;
        _subscription?.cancel();
        _subscription = null;
        _connectionController.add(false);
        if (_autoReconnect) {
          _tryReconnect(device);
        }
      }
    });

    final services = await device.discoverServices();
    for (final service in services) {
      if (service.uuid.toString() == canServiceUuid) {
        for (final char in service.characteristics) {
          final uuid = char.uuid.toString();
          if (uuid == vehicleDataCharUuid) {
            try { await char.setNotifyValue(true); } catch (_) {}
            _subscription?.cancel();
            _subscription = char.lastValueStream.listen((data) {
              if (data.isNotEmpty) {
                _rawDataController.add(data);
                final telemetry = Telemetry.fromVehicleData(data);
                _telemetryController.add(telemetry);
              }
            });
          } else if (uuid == canFrameCharUuid) {
            try { await char.setNotifyValue(true); } catch (_) {}
            if (_subscription == null) {
              _subscription = char.lastValueStream.listen((data) {
                if (data.isNotEmpty) {
                  _rawDataController.add(data);
                  final telemetry = Telemetry.fromBleData(data);
                  _telemetryController.add(telemetry);
                }
              });
            }
          } else if (uuid == canCtrlCharUuid) {
            _ctrlChar = char;
          } else if (uuid == otaDataCharUuid) {
            _otaDataChar = char;
          } else if (uuid == fwVersionCharUuid) {
            _fwVersionChar = char;
          } else if (uuid == mgmtCharUuid) {
            _mgmtChar = char;
          }
        }
      }
    }
  }

  Future<void> startLiveNotify() async {
    for (int i = 0; i < 5; i++) {
      try {
        await _ctrlChar?.write([0x01]);
        return;
      } catch (_) {
        await Future.delayed(Duration(milliseconds: 300 * (i + 1)));
      }
    }
  }

  Future<void> stopLiveNotify() async {
    await _ctrlChar?.write([0x00]);
  }

  Future<bool> ping() async {
    if (_ctrlChar == null) return false;
    try {
      await _ctrlChar!.write([0x02]);
      return true;
    } catch (_) {
      return false;
    }
  }

  Future<void> _tryReconnect(BluetoothDevice device) async {
    for (int i = 0; i < 3; i++) {
      if (!_autoReconnect || _device != null) return;
      await Future.delayed(Duration(seconds: 3 + i * 2));
      try {
        final results = await scan(timeout: const Duration(seconds: 3));
        if (results.isEmpty || !_autoReconnect) continue;
        await connect(results.first.device);
        return;
      } catch (_) {}
    }
  }

  Future<Map<String, dynamic>?> readMgmtInfo() async {
    if (_mgmtChar == null) return null;
    try {
      final d = await _mgmtChar!.read();
      if (d.length < 16) return null;
      return {
        'uptime_h': d[0],
        'uptime_m': d[1],
        'heap_kb': d[2] | (d[3] << 8),
        'board_temp': d[4] - 40,
        'log_records': d[5] | (d[6] << 8),
        'log_used_kb': d[7] | (d[8] << 8),
        'log_free_kb': d[9] | (d[10] << 8),
        'trip_count': d[11],
        'trip_active': d[12] == 1,
        'peer_known': d[13] == 1,
        'ota_state': d[14],
      };
    } catch (_) {
      return null;
    }
  }

  Future<bool> clearLogs() async {
    if (_mgmtChar == null) return false;
    try {
      await _mgmtChar!.write([0x01]);
      return true;
    } catch (_) {
      return false;
    }
  }

  Future<bool> restartDevice() async {
    if (_mgmtChar == null) return false;
    try {
      await _mgmtChar!.write([0x02]);
      return true;
    } catch (_) {
      return false;
    }
  }

  Future<String?> readFirmwareVersion() async {
    if (_fwVersionChar == null) return null;
    try {
      final data = await _fwVersionChar!.read();
      return String.fromCharCodes(data);
    } catch (_) {
      return null;
    }
  }

  Future<bool> startOta(int firmwareSize) async {
    if (_ctrlChar == null) return false;
    try {
      final sizeBytes = [
        0x20,
        firmwareSize & 0xFF,
        (firmwareSize >> 8) & 0xFF,
        (firmwareSize >> 16) & 0xFF,
        (firmwareSize >> 24) & 0xFF,
      ];
      await _ctrlChar!.write(sizeBytes);
      return true;
    } catch (_) {
      return false;
    }
  }

  Future<bool> writeOtaChunk(List<int> chunk) async {
    if (_otaDataChar == null) return false;
    try {
      await _otaDataChar!.write(chunk, withoutResponse: true);
      return true;
    } catch (_) {
      return false;
    }
  }

  Future<bool> finishOta() async {
    if (_ctrlChar == null) return false;
    try {
      await _ctrlChar!.write([0x21]);
      return true;
    } catch (_) {
      return false;
    }
  }

  Future<bool> abortOta() async {
    if (_ctrlChar == null) return false;
    try {
      await _ctrlChar!.write([0x22]);
      return true;
    } catch (_) {
      return false;
    }
  }

  Future<void> disconnect() async {
    _autoReconnect = false;
    _connStateSub?.cancel();
    await _subscription?.cancel();
    await _device?.disconnect();
    _device = null;
    _ctrlChar = null;
    _otaDataChar = null;
    _fwVersionChar = null;
    _mgmtChar = null;
    _connectionController.add(false);
  }

  void dispose() {
    _autoReconnect = false;
    _connStateSub?.cancel();
    _subscription?.cancel();
    _telemetryController.close();
    _rawDataController.close();
    _connectionController.close();
  }
}
