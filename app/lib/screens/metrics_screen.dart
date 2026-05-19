import 'package:flutter/material.dart';
import '../services/database_service.dart';
import '../models/telemetry.dart';

class MetricsScreen extends StatefulWidget {
  const MetricsScreen({super.key});

  @override
  State<MetricsScreen> createState() => _MetricsScreenState();
}

class _MetricsScreenState extends State<MetricsScreen> {
  final _db = DatabaseService();
  List<Telemetry> _data = [];
  String _range = 'all';
  bool _loading = true;
  bool _isLegacy = false;

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    setState(() => _loading = true);
    final all = await _db.getRecent(limit: 10000);

    // Separate old raw data (v1: rpm > 20000 = CAN ID) from decoded data
    final decoded = all.where((t) => t.rpm < 20000).toList();
    final rawLegacy = all.where((t) => t.rpm >= 20000).toList();

    if (_range == 'all') {
      _data = decoded.isNotEmpty ? decoded : rawLegacy;
      _isLegacy = decoded.isEmpty && rawLegacy.isNotEmpty;
    } else {
      final mins = int.tryParse(_range) ?? 9999;
      final cutoff = DateTime.now().subtract(Duration(minutes: mins));
      final filtered = decoded.where((t) => t.timestamp.isAfter(cutoff)).toList();
      _data = filtered;
      _isLegacy = false;
    }
    setState(() => _loading = false);
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.grey[900],
      appBar: AppBar(
        title: const Text('Trip Metrics'),
        backgroundColor: Colors.black87,
        foregroundColor: Colors.white,
      ),
      body: Column(
        children: [
          // Time range selector
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 6),
            color: Colors.grey[850],
            child: Row(
              children: [
                _chip('5m', '5'),
                _chip('30m', '30'),
                _chip('1h', '60'),
                _chip('All', 'all'),
                const Spacer(),
                Text(
                  '${_data.length} samples',
                  style: const TextStyle(color: Colors.white38, fontSize: 12),
                ),
              ],
            ),
          ),
          Expanded(
            child: _loading
                ? const Center(child: CircularProgressIndicator())
                : _data.isEmpty
                    ? const Center(
                        child: Text(
                          'No data recorded yet\nConnect to vehicle and press Record',
                          textAlign: TextAlign.center,
                          style: TextStyle(color: Colors.white38),
                        ),
                      )
                    : _isLegacy
                        ? _buildLegacyView()
                        : ListView(
                            padding: const EdgeInsets.all(8),
                            children: [
                              _timeCard(),
                              _sensorCard('Speed', 'km/h', _data.map((t) => t.speed.toDouble()).toList(), Colors.blue),
                              _sensorCard('RPM', '', _data.map((t) => t.rpm.toDouble()).toList(), Colors.orange),
                              _sensorCard('Throttle', '%', _data.map((t) => t.throttle.toDouble()).toList(), Colors.green),
                              _sensorCard('Coolant', '°C', _data.map((t) => t.coolantTemp.toDouble()).toList(), Colors.red),
                              _sensorCard('MAP', 'kPa', _data.map((t) => t.mapKpa.toDouble()).toList(), Colors.purple),
                              _sensorCard('IAT', '°C', _data.map((t) => t.iat.toDouble()).toList(), Colors.cyan),
                              _sensorCard('Load', '%', _data.map((t) => t.engineLoad.toDouble()).toList(), Colors.amber),
                            ],
                          ),
          ),
        ],
      ),
    );
  }

  static const _didNames = {
    0x02: 'Freeze DTC',
    0x04: 'Engine Load',
    0x05: 'Coolant Temp',
    0x06: 'Fuel Trim',
    0x0B: 'MAP',
    0x0C: 'RPM',
    0x0D: 'Speed',
    0x0E: 'Ign Timing',
    0x0F: 'IAT',
    0x11: 'Throttle',
    0x12: 'Air Status',
    0x1C: 'OBD Compliance',
  };

  Widget _buildLegacyView() {
    // Decode old raw UDS frames: throttle=PCI, coolantTemp=SID, gear=DID_H, fuelLevel=DID_L
    final positiveFrames = _data.where((t) => t.coolantTemp == 0x62).toList(); // SID=0x62
    final nrcFrames = _data.where((t) => t.coolantTemp == 0x7F).toList();

    // Count DIDs queried
    final didCounts = <int, int>{};
    for (final t in positiveFrames) {
      // gear was renamed to raw_gear but fromMap reads it — actually the old schema
      // has gear column. fuelLevel = DID low byte
      // In old data: throttle=PCI, coolantTemp=SID, gear/raw_gear=DID_H(0xF4), fuelLevel/raw_fuel=DID_L
      // But after migration, gear→raw_gear, fuelLevel→raw_fuel, these aren't in the model anymore
      // We still have the speed field = DLC = 8 always
      // Actually the old Telemetry.fromMap would fail on new columns...
      // The data we have: rpm=CANID, speed=DLC, throttle=PCI, coolantTemp=SID
      // mapKpa/iat/engineLoad/ignitionTiming = 0 (new columns, default)
    }

    return ListView(
      padding: const EdgeInsets.all(8),
      children: [
        _timeCard(),
        Card(
          color: Colors.amber[900],
          child: Padding(
            padding: const EdgeInsets.all(12),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Row(
                  children: [
                    Icon(Icons.warning, color: Colors.white, size: 18),
                    SizedBox(width: 8),
                    Text('Legacy Raw Data', style: TextStyle(color: Colors.white, fontWeight: FontWeight.bold)),
                  ],
                ),
                const SizedBox(height: 6),
                const Text(
                  'This data was recorded before UDS decoding was implemented. '
                  'It contains raw CAN frame headers (CAN ID, DLC, PCI, SID) '
                  'but NOT the actual sensor values. Sensor values were not captured.',
                  style: TextStyle(color: Colors.white70, fontSize: 12),
                ),
                const SizedBox(height: 8),
                Text('Total frames: ${_data.length}', style: const TextStyle(color: Colors.white, fontSize: 13)),
                Text('Positive responses (0x62): ${positiveFrames.length}', style: const TextStyle(color: Colors.greenAccent, fontSize: 13)),
                Text('Negative responses (0x7F): ${nrcFrames.length}', style: const TextStyle(color: Colors.redAccent, fontSize: 13)),
                const SizedBox(height: 8),
                const Text('Record new data with the updated firmware to get decoded sensor values.',
                    style: TextStyle(color: Colors.white54, fontSize: 11)),
              ],
            ),
          ),
        ),
      ],
    );
  }

  Widget _chip(String label, String value) {
    final selected = _range == value;
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 3),
      child: ChoiceChip(
        label: Text(label, style: TextStyle(fontSize: 12, color: selected ? Colors.white : Colors.white54)),
        selected: selected,
        selectedColor: Colors.blue[700],
        backgroundColor: Colors.grey[800],
        onSelected: (_) {
          _range = value;
          _load();
        },
      ),
    );
  }

  Widget _timeCard() {
    if (_data.isEmpty) return const SizedBox.shrink();
    final first = _data.last.timestamp;
    final last = _data.first.timestamp;
    final dur = last.difference(first);
    final mins = dur.inMinutes;
    final secs = dur.inSeconds % 60;

    return Card(
      color: Colors.grey[800],
      child: Padding(
        padding: const EdgeInsets.all(12),
        child: Row(
          children: [
            const Icon(Icons.timer, color: Colors.white54, size: 20),
            const SizedBox(width: 8),
            Text(
              'Duration: ${mins}m ${secs}s',
              style: const TextStyle(color: Colors.white70, fontSize: 14),
            ),
            const Spacer(),
            Text(
              '${_fmt(first)} — ${_fmt(last)}',
              style: const TextStyle(color: Colors.white38, fontSize: 11),
            ),
          ],
        ),
      ),
    );
  }

  String _fmt(DateTime dt) =>
      '${dt.hour.toString().padLeft(2, '0')}:${dt.minute.toString().padLeft(2, '0')}:${dt.second.toString().padLeft(2, '0')}';

  Widget _sensorCard(String name, String unit, List<double> values, Color color) {
    if (values.isEmpty) return const SizedBox.shrink();

    final nonZero = values.where((v) => v != 0).toList();
    final max = values.reduce((a, b) => a > b ? a : b);
    final min = nonZero.isEmpty ? 0.0 : nonZero.reduce((a, b) => a < b ? a : b);
    final avg = values.isEmpty ? 0.0 : values.reduce((a, b) => a + b) / values.length;

    // Simple sparkline with last 60 values
    final spark = values.length > 60 ? values.sublist(0, 60) : values;

    return Card(
      color: Colors.grey[850],
      child: Padding(
        padding: const EdgeInsets.all(12),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Text(name, style: TextStyle(color: color, fontSize: 14, fontWeight: FontWeight.bold)),
                if (unit.isNotEmpty) Text(' $unit', style: const TextStyle(color: Colors.white38, fontSize: 11)),
                const Spacer(),
                Text('now: ${values.first.round()}', style: TextStyle(color: color, fontSize: 13)),
              ],
            ),
            const SizedBox(height: 8),
            Row(
              children: [
                _statBox('MAX', max, color),
                _statBox('AVG', avg, Colors.white54),
                _statBox('MIN', min, Colors.white38),
              ],
            ),
            const SizedBox(height: 6),
            SizedBox(
              height: 30,
              child: CustomPaint(
                size: const Size(double.infinity, 30),
                painter: _SparkPainter(spark, color),
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _statBox(String label, double value, Color color) {
    return Expanded(
      child: Column(
        children: [
          Text(label, style: const TextStyle(color: Colors.white24, fontSize: 9)),
          Text('${value.round()}', style: TextStyle(color: color, fontSize: 16, fontWeight: FontWeight.bold)),
        ],
      ),
    );
  }
}

class _SparkPainter extends CustomPainter {
  final List<double> values;
  final Color color;

  _SparkPainter(this.values, this.color);

  @override
  void paint(Canvas canvas, Size size) {
    if (values.length < 2) return;

    final max = values.reduce((a, b) => a > b ? a : b);
    final min = values.reduce((a, b) => a < b ? a : b);
    final range = max - min;
    if (range == 0) return;

    final paint = Paint()
      ..color = color.withValues(alpha: 0.6)
      ..strokeWidth = 1.5
      ..style = PaintingStyle.stroke;

    final path = Path();
    for (int i = 0; i < values.length; i++) {
      final x = i / (values.length - 1) * size.width;
      final y = size.height - ((values[i] - min) / range) * size.height;
      if (i == 0) {
        path.moveTo(x, y);
      } else {
        path.lineTo(x, y);
      }
    }
    canvas.drawPath(path, paint);
  }

  @override
  bool shouldRepaint(covariant CustomPainter oldDelegate) => true;
}
