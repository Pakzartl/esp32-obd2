import 'dart:async';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import '../models/telemetry.dart';

class BleService {
  static const String canServiceUuid =
      '12345678-1234-5678-1234-56789abcdef0';
  static const String canFrameCharUuid =
      '12345678-1234-5678-1234-56789abcdef1';
  static const String canCtrlCharUuid =
      '12345678-1234-5678-1234-56789abcdef2';
  static const String deviceName = 'ADV350';

  BluetoothDevice? _device;
  BluetoothCharacteristic? _ctrlChar;
  StreamSubscription? _subscription;
  StreamSubscription? _connStateSub;
  bool _autoReconnect = true;
  final _telemetryController = StreamController<Telemetry>.broadcast();
  final _connectionController = StreamController<bool>.broadcast();

  Stream<Telemetry> get telemetryStream => _telemetryController.stream;
  Stream<bool> get connectionStream => _connectionController.stream;
  bool get isConnected => _device != null;

  Future<List<ScanResult>> scan(
      {Duration timeout = const Duration(seconds: 5)}) async {
    final results = <ScanResult>[];

    final sub = FlutterBluePlus.scanResults.listen((r) {
      for (final result in r) {
        if (result.device.platformName == deviceName &&
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
      timeout: const Duration(seconds: 10),
    );
    _device = device;
    _autoReconnect = true;
    _connectionController.add(true);

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

    try {
      await device.requestMtu(256);
    } catch (_) {}

    final services = await device.discoverServices();
    for (final service in services) {
      if (service.uuid.toString() == canServiceUuid) {
        for (final char in service.characteristics) {
          final uuid = char.uuid.toString();
          if (uuid == canFrameCharUuid) {
            await char.setNotifyValue(true);
            _subscription = char.lastValueStream.listen((data) {
              if (data.isNotEmpty) {
                final telemetry = Telemetry.fromBleData(data);
                _telemetryController.add(telemetry);
              }
            });
          } else if (uuid == canCtrlCharUuid) {
            _ctrlChar = char;
          }
        }
      }
    }
  }

  Future<void> startLiveNotify() async {
    await _ctrlChar?.write([0x01]);
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
    for (int i = 0; i < 5; i++) {
      if (!_autoReconnect || _device != null) return;
      await Future.delayed(Duration(seconds: 2 + i));
      try {
        await connect(device);
        return;
      } catch (_) {}
    }
  }

  Future<void> disconnect() async {
    _autoReconnect = false;
    _connStateSub?.cancel();
    await _subscription?.cancel();
    await _device?.disconnect();
    _device = null;
    _ctrlChar = null;
    _connectionController.add(false);
  }

  void dispose() {
    _autoReconnect = false;
    _connStateSub?.cancel();
    _subscription?.cancel();
    _telemetryController.close();
    _connectionController.close();
  }
}
