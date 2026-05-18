class Telemetry {
  final int? id;
  final int rpm;
  final int speed;
  final int throttle;
  final int coolantTemp;
  final int gear;
  final int fuelLevel;
  final DateTime timestamp;
  final bool synced;

  Telemetry({
    this.id,
    required this.rpm,
    required this.speed,
    required this.throttle,
    required this.coolantTemp,
    required this.gear,
    required this.fuelLevel,
    required this.timestamp,
    this.synced = false,
  });

  Map<String, dynamic> toMap() => {
        if (id != null) 'id': id,
        'rpm': rpm,
        'speed': speed,
        'throttle': throttle,
        'coolant_temp': coolantTemp,
        'gear': gear,
        'fuel_level': fuelLevel,
        'timestamp': timestamp.millisecondsSinceEpoch,
        'synced': synced ? 1 : 0,
      };

  factory Telemetry.fromMap(Map<String, dynamic> map) => Telemetry(
        id: map['id'] as int?,
        rpm: map['rpm'] as int,
        speed: map['speed'] as int,
        throttle: map['throttle'] as int,
        coolantTemp: map['coolant_temp'] as int,
        gear: map['gear'] as int,
        fuelLevel: map['fuel_level'] as int,
        timestamp:
            DateTime.fromMillisecondsSinceEpoch(map['timestamp'] as int),
        synced: (map['synced'] as int) == 1,
      );

  factory Telemetry.fromBleData(List<int> data) {
    if (data.length < 5) {
      return Telemetry.empty();
    }
    // Raw CAN frame: [ID(4 LE)][DLC(1)][DATA(0-8)]
    final canId = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
    final dlc = data[4];
    final canData = data.length > 5 ? data.sublist(5) : <int>[];

    return Telemetry(
      rpm: canId,
      speed: dlc,
      throttle: canData.isNotEmpty ? canData[0] : 0,
      coolantTemp: canData.length > 1 ? canData[1] : 0,
      gear: canData.length > 2 ? canData[2] : 0,
      fuelLevel: canData.length > 3 ? canData[3] : 0,
      timestamp: DateTime.now(),
    );
  }

  factory Telemetry.empty() => Telemetry(
        rpm: 0,
        speed: 0,
        throttle: 0,
        coolantTemp: 0,
        gear: 0,
        fuelLevel: 0,
        timestamp: DateTime.now(),
      );
}
