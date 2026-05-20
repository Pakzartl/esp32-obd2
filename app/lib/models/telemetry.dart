class Telemetry {
  final int? id;
  final int rpm;
  final int speed;
  final int throttle;
  final int coolantTemp;
  final int mapKpa;
  final int iat;
  final int engineLoad;
  final int ignitionTiming;
  final String rawBleHex;
  final DateTime timestamp;
  final bool synced;

  Telemetry({
    this.id,
    required this.rpm,
    required this.speed,
    required this.throttle,
    required this.coolantTemp,
    required this.mapKpa,
    required this.iat,
    required this.engineLoad,
    required this.ignitionTiming,
    this.rawBleHex = '',
    required this.timestamp,
    this.synced = false,
  });

  // Accumulated state from multiple UDS frames
  static int _rpm = 0;
  static int _speed = 0;
  static int _throttle = 0;
  static int _coolantTemp = 0;
  static int _mapKpa = 0;
  static int _iat = 0;
  static int _engineLoad = 0;
  static int _ignitionTiming = 0;
  static String _lastRawHex = '';

  // Parse 16-byte packed vehicle data from S3 relay (def3 characteristic)
  // [flags:2][rpm:2][speed:1][coolant+40:1][throttle*2.55:1][map:1][iat+40:1][batt*100:2][fuel_rate*100:2][cvt*100:2][score:1]
  factory Telemetry.fromVehicleData(List<int> data) {
    if (data.length < 16) return Telemetry.empty();

    _lastRawHex = data.map((b) => b.toRadixString(16).padLeft(2, '0')).join('');

    final flags = data[0] | (data[1] << 8);
    if (flags & 0x01 != 0) _rpm = data[2] | (data[3] << 8);
    if (flags & 0x02 != 0) _speed = data[4];
    if (flags & 0x04 != 0) _coolantTemp = data[5] - 40;
    if (flags & 0x08 != 0) _throttle = (data[6] * 100 / 255).round();
    if (flags & 0x10 != 0) _mapKpa = data[7];
    if (flags & 0x20 != 0) _iat = data[8] - 40;

    return Telemetry._current();
  }

  factory Telemetry.fromBleData(List<int> data) {
    if (data.length < 5) return Telemetry.empty();

    // Always store full raw BLE packet as hex
    _lastRawHex = data.map((b) => b.toRadixString(16).padLeft(2, '0')).join('');

    final canData = data.length > 5 ? data.sublist(5) : <int>[];
    if (canData.length < 4) return Telemetry._current();

    final pciLen = canData[0] & 0x0F;
    final sid = canData[1];

    // UDS positive response: SID=0x62 (ReadDataByIdentifier response)
    if (sid == 0x62 && pciLen >= 3 && canData.length >= 4) {
      final did = (canData[2] << 8) | canData[3];

      switch (did) {
        case 0xF40C: // RPM
          if (canData.length >= 6) {
            _rpm = ((canData[4] * 256 + canData[5]) / 4).round();
          }
          break;
        case 0xF40D: // Speed
          if (canData.length >= 5) _speed = canData[4];
          break;
        case 0xF411: // Throttle
          if (canData.length >= 5) _throttle = (canData[4] * 100 / 255).round();
          break;
        case 0xF405: // Coolant temp
          if (canData.length >= 5) _coolantTemp = canData[4] - 40;
          break;
        case 0xF40B: // MAP
          if (canData.length >= 5) _mapKpa = canData[4];
          break;
        case 0xF40F: // IAT
          if (canData.length >= 5) _iat = canData[4] - 40;
          break;
        case 0xF404: // Engine load
          if (canData.length >= 5) _engineLoad = (canData[4] * 100 / 255).round();
          break;
        case 0xF40E: // Ignition timing
          if (canData.length >= 5) _ignitionTiming = (canData[4] / 2 - 64).round();
          break;
      }
    }

    return Telemetry._current();
  }

  factory Telemetry._current() => Telemetry(
        rpm: _rpm,
        speed: _speed,
        throttle: _throttle,
        coolantTemp: _coolantTemp,
        mapKpa: _mapKpa,
        iat: _iat,
        engineLoad: _engineLoad,
        ignitionTiming: _ignitionTiming,
        rawBleHex: _lastRawHex,
        timestamp: DateTime.now(),
      );

  factory Telemetry.empty() => Telemetry(
        rpm: 0,
        speed: 0,
        throttle: 0,
        coolantTemp: 0,
        mapKpa: 0,
        iat: 0,
        engineLoad: 0,
        ignitionTiming: 0,
        rawBleHex: '',
        timestamp: DateTime.now(),
      );

  Map<String, dynamic> toMap() => {
        if (id != null) 'id': id,
        'rpm': rpm,
        'speed': speed,
        'throttle': throttle,
        'coolant_temp': coolantTemp,
        'raw_gear': 0,
        'raw_fuel': 0,
        'map_kpa': mapKpa,
        'iat': iat,
        'engine_load': engineLoad,
        'ignition_timing': ignitionTiming,
        'raw_ble_hex': rawBleHex,
        'timestamp': timestamp.millisecondsSinceEpoch,
        'synced': synced ? 1 : 0,
      };

  factory Telemetry.fromMap(Map<String, dynamic> map) => Telemetry(
        id: map['id'] as int?,
        rpm: map['rpm'] as int,
        speed: map['speed'] as int,
        throttle: map['throttle'] as int,
        coolantTemp: map['coolant_temp'] as int,
        mapKpa: (map['map_kpa'] as int?) ?? 0,
        iat: (map['iat'] as int?) ?? 0,
        engineLoad: (map['engine_load'] as int?) ?? 0,
        ignitionTiming: (map['ignition_timing'] as int?) ?? 0,
        rawBleHex: (map['raw_ble_hex'] as String?) ?? '',
        timestamp:
            DateTime.fromMillisecondsSinceEpoch(map['timestamp'] as int),
        synced: (map['synced'] as int) == 1,
      );
}
