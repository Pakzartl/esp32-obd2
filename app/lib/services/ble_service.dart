import 'dart:async';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import '../models/telemetry.dart';

class BleService {
  static const String serviceUuid = '000000ff-0000-1000-8000-00805f9b34fb';
  static const String telemetryCharUuid = '0000ff01-0000-1000-8000-00805f9b34fb';

  BluetoothDevice? _device;
  StreamSubscription? _subscription;
  final _telemetryController = StreamController<Telemetry>.broadcast();
  final _connectionController = StreamController<bool>.broadcast();

  Stream<Telemetry> get telemetryStream => _telemetryController.stream;
  Stream<bool> get connectionStream => _connectionController.stream;
  bool get isConnected => _device != null;

  Future<List<ScanResult>> scan({Duration timeout = const Duration(seconds: 5)}) async {
    final results = <ScanResult>[];

    final sub = FlutterBluePlus.scanResults.listen((r) {
      for (final result in r) {
        if (!results.any((e) => e.device.remoteId == result.device.remoteId)) {
          results.add(result);
        }
      }
    });

    await FlutterBluePlus.startScan(
      withServices: [Guid(serviceUuid)],
      timeout: timeout,
    );

    await Future.delayed(timeout);
    await sub.cancel();
    return results;
  }

  Future<void> connect(BluetoothDevice device) async {
    await device.connect(license: License.free, autoConnect: false);
    _device = device;
    _connectionController.add(true);

    device.connectionState.listen((state) {
      if (state == BluetoothConnectionState.disconnected) {
        _device = null;
        _connectionController.add(false);
      }
    });

    final services = await device.discoverServices();
    for (final service in services) {
      if (service.uuid.toString() == serviceUuid) {
        for (final char in service.characteristics) {
          if (char.uuid.toString() == telemetryCharUuid) {
            await char.setNotifyValue(true);
            _subscription = char.lastValueStream.listen((data) {
              if (data.isNotEmpty) {
                final telemetry = Telemetry.fromBleData(data);
                _telemetryController.add(telemetry);
              }
            });
          }
        }
      }
    }
  }

  Future<void> disconnect() async {
    await _subscription?.cancel();
    await _device?.disconnect();
    _device = null;
    _connectionController.add(false);
  }

  void dispose() {
    _subscription?.cancel();
    _telemetryController.close();
    _connectionController.close();
  }
}
