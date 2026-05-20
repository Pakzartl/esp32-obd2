import 'package:flutter/material.dart';
import '../../models/telemetry.dart';
import '../../services/database_service.dart';

class TripTab extends StatefulWidget {
  final Telemetry current;

  const TripTab({super.key, required this.current});

  @override
  State<TripTab> createState() => _TripTabState();
}

class _TripTabState extends State<TripTab> {
  final _db = DatabaseService();
  List<Telemetry> _data = [];
  bool _loading = true;
  String _range = '30';
  DateTime? _customStart;
  DateTime? _customEnd;

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    setState(() => _loading = true);
    final all = await _db.getRecent(limit: 10000);
    var decoded = all.where((t) => t.rpm < 20000).toList();

    if (_range == 'custom' && _customStart != null && _customEnd != null) {
      decoded = decoded.where((t) =>
          t.timestamp.isAfter(_customStart!) &&
          t.timestamp.isBefore(_customEnd!)).toList();
    } else if (_range != 'all') {
      final mins = int.tryParse(_range) ?? 9999;
      final cutoff = DateTime.now().subtract(Duration(minutes: mins));
      decoded = decoded.where((t) => t.timestamp.isAfter(cutoff)).toList();
    }
    _data = decoded.reversed.toList();
    setState(() => _loading = false);
  }

  Future<void> _pickDateTimeRange() async {
    final now = DateTime.now();
    final dateRange = await showDateRangePicker(
      context: context,
      firstDate: now.subtract(const Duration(days: 365)),
      lastDate: now,
      initialDateRange: DateTimeRange(
        start: _customStart ?? now.subtract(const Duration(hours: 1)),
        end: _customEnd ?? now,
      ),
      builder: (context, child) => Theme(
        data: Theme.of(context).copyWith(
          colorScheme: ColorScheme.dark(
            primary: Colors.blue,
            surface: Colors.grey[900]!,
          ),
        ),
        child: child!,
      ),
    );
    if (dateRange == null) return;

    final startTime = await showTimePicker(
      context: context,
      initialTime: TimeOfDay.fromDateTime(
          _customStart ?? dateRange.start),
      builder: (context, child) => Theme(
        data: Theme.of(context).copyWith(
          colorScheme: ColorScheme.dark(
            primary: Colors.blue,
            surface: Colors.grey[900]!,
          ),
        ),
        child: child!,
      ),
    );
    if (startTime == null) return;

    final endTime = await showTimePicker(
      context: context,
      initialTime: TimeOfDay.fromDateTime(
          _customEnd ?? dateRange.end),
      builder: (context, child) => Theme(
        data: Theme.of(context).copyWith(
          colorScheme: ColorScheme.dark(
            primary: Colors.blue,
            surface: Colors.grey[900]!,
          ),
        ),
        child: child!,
      ),
    );
    if (endTime == null) return;

    _customStart = DateTime(
      dateRange.start.year, dateRange.start.month, dateRange.start.day,
      startTime.hour, startTime.minute,
    );
    _customEnd = DateTime(
      dateRange.end.year, dateRange.end.month, dateRange.end.day,
      endTime.hour, endTime.minute,
    );
    _range = 'custom';
    _load();
  }

  @override
  Widget build(BuildContext context) {
    return _loading
        ? const Center(child: CircularProgressIndicator(color: Colors.blue))
        : ListView(
            padding: const EdgeInsets.all(16),
            children: [
              // Time range chips
              _buildRangeChips(),
              const SizedBox(height: 16),
              // Trip summary cards
              _buildTripSummary(),
              const SizedBox(height: 16),
              // Sensor stats
              if (_data.isNotEmpty) ...[
                _sensorCard('Speed', 'km/h',
                    _data.map((t) => t.speed.toDouble()).toList(), Colors.blue),
                _sensorCard('RPM', '',
                    _data.map((t) => t.rpm.toDouble()).toList(), Colors.orange),
                _sensorCard(
                    'Throttle',
                    '%',
                    _data.map((t) => t.throttle.toDouble()).toList(),
                    Colors.green),
                _sensorCard(
                    'Coolant',
                    '°C',
                    _data.map((t) => t.coolantTemp.toDouble()).toList(),
                    Colors.red),
                _sensorCard('MAP', 'kPa',
                    _data.map((t) => t.mapKpa.toDouble()).toList(),
                    Colors.purple),
                _sensorCard('IAT', '°C',
                    _data.map((t) => t.iat.toDouble()).toList(),
                    Colors.teal),
                _sensorCard('Fuel Rate (est.)', 'L/h',
                    _data.map((t) => t.fuelRateLph).toList(),
                    Colors.orange),
                _sensorCard('CVT Ratio', '',
                    _data.map((t) => t.cvtRatio).toList(),
                    Colors.indigo),
                _sensorCard('Ride Score', '/100',
                    _data.map((t) => t.ridingScore.toDouble()).toList(),
                    Colors.greenAccent),
              ],
              if (_data.isEmpty)
                Padding(
                  padding: const EdgeInsets.only(top: 80),
                  child: Center(
                    child: Text(
                      'No trip data yet\nConnect and ride to see stats',
                      textAlign: TextAlign.center,
                      style: TextStyle(color: Colors.grey[600], fontSize: 15),
                    ),
                  ),
                ),
            ],
          );
  }

  Widget _buildRangeChips() {
    String rangeLabel = '';
    if (_range == 'custom' && _customStart != null && _customEnd != null) {
      String fmt(DateTime d) =>
          '${d.month}/${d.day} ${d.hour.toString().padLeft(2, '0')}:${d.minute.toString().padLeft(2, '0')}';
      rangeLabel = '${fmt(_customStart!)} — ${fmt(_customEnd!)}';
    }

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Wrap(
          spacing: 6,
          runSpacing: 4,
          children: [
            _chip('5m', '5'),
            _chip('15m', '15'),
            _chip('30m', '30'),
            _chip('1h', '60'),
            _chip('6h', '360'),
            _chip('24h', '1440'),
            _chip('All', 'all'),
            _chipCustom(),
          ],
        ),
        if (rangeLabel.isNotEmpty)
          Padding(
            padding: const EdgeInsets.only(top: 6),
            child: Text(rangeLabel,
                style: TextStyle(color: Colors.blue[300], fontSize: 11)),
          ),
        Padding(
          padding: const EdgeInsets.only(top: 4),
          child: Text(
            '${_data.length} pts',
            style: TextStyle(color: Colors.grey[600], fontSize: 11),
          ),
        ),
      ],
    );
  }

  Widget _chip(String label, String value) {
    final selected = _range == value;
    return ChoiceChip(
      label: Text(label,
          style: TextStyle(
              fontSize: 12,
              color: selected ? Colors.white : Colors.grey[500])),
      selected: selected,
      selectedColor: Colors.blue[700],
      backgroundColor: Colors.grey[800],
      onSelected: (_) {
        _range = value;
        _load();
      },
      visualDensity: VisualDensity.compact,
    );
  }

  Widget _chipCustom() {
    final selected = _range == 'custom';
    return ChoiceChip(
      avatar: Icon(Icons.date_range,
          size: 14, color: selected ? Colors.white : Colors.grey[500]),
      label: Text('Custom',
          style: TextStyle(
              fontSize: 12,
              color: selected ? Colors.white : Colors.grey[500])),
      selected: selected,
      selectedColor: Colors.blue[700],
      backgroundColor: Colors.grey[800],
      onSelected: (_) => _pickDateTimeRange(),
      visualDensity: VisualDensity.compact,
    );
  }

  Widget _buildTripSummary() {
    if (_data.isEmpty) return const SizedBox.shrink();

    final first = _data.first.timestamp;
    final last = _data.last.timestamp;
    final dur = last.difference(first);
    final mins = dur.inMinutes;
    final secs = dur.inSeconds % 60;

    // Calculate trip metrics
    final speeds = _data.map((t) => t.speed.toDouble()).toList();
    final avgSpeed =
        speeds.isEmpty ? 0.0 : speeds.reduce((a, b) => a + b) / speeds.length;
    final maxSpeed =
        speeds.isEmpty ? 0.0 : speeds.reduce((a, b) => a > b ? a : b);

    // Estimate trip distance (speed samples * interval)
    // Each sample ~1 second apart, speed in km/h -> km/3600
    double distKm = 0;
    for (final s in speeds) {
      distKm += s / 3600;
    }

    // Riding score: based on smooth throttle transitions
    final throttles = _data.map((t) => t.throttle.toDouble()).toList();
    double smoothness = 100;
    if (throttles.length > 1) {
      double totalDelta = 0;
      for (int i = 1; i < throttles.length; i++) {
        totalDelta += (throttles[i] - throttles[i - 1]).abs();
      }
      final avgDelta = totalDelta / (throttles.length - 1);
      smoothness = (100 - avgDelta * 5).clamp(0, 100);
    }

    return Column(
      children: [
        // Duration + distance row
        Row(
          children: [
            Expanded(
              child: _summaryCard(
                Icons.timer_outlined,
                'Duration',
                '${mins}m ${secs}s',
                Colors.blue,
              ),
            ),
            const SizedBox(width: 12),
            Expanded(
              child: _summaryCard(
                Icons.route,
                'Distance',
                '${distKm.toStringAsFixed(1)} km',
                Colors.green,
              ),
            ),
          ],
        ),
        const SizedBox(height: 12),
        // Speed + score row
        Row(
          children: [
            Expanded(
              child: _summaryCard(
                Icons.speed,
                'Avg Speed',
                '${avgSpeed.round()} km/h',
                Colors.cyan,
              ),
            ),
            const SizedBox(width: 12),
            Expanded(
              child: _summaryCard(
                Icons.star_outline,
                'Ride Score',
                '${smoothness.round()}',
                _scoreColor(smoothness),
              ),
            ),
          ],
        ),
        const SizedBox(height: 12),
        Row(
          children: [
            Expanded(
              child: _summaryCard(
                Icons.trending_up,
                'Max Speed',
                '${maxSpeed.round()} km/h',
                Colors.orange,
              ),
            ),
            const SizedBox(width: 12),
            Expanded(
              child: _summaryCard(
                Icons.data_usage,
                'Samples',
                '${_data.length}',
                Colors.purple,
              ),
            ),
          ],
        ),
      ],
    );
  }

  Color _scoreColor(double score) {
    if (score >= 80) return Colors.greenAccent;
    if (score >= 50) return Colors.orange;
    return Colors.redAccent;
  }

  Widget _summaryCard(
      IconData icon, String label, String value, Color color) {
    return Container(
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: Colors.grey[850],
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: color.withValues(alpha: 0.2)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(icon, color: color.withValues(alpha: 0.7), size: 16),
              const SizedBox(width: 6),
              Text(label,
                  style: TextStyle(color: Colors.grey[500], fontSize: 11)),
            ],
          ),
          const SizedBox(height: 8),
          Text(
            value,
            style: TextStyle(
              color: color,
              fontSize: 20,
              fontWeight: FontWeight.bold,
              fontFamily: 'monospace',
            ),
          ),
        ],
      ),
    );
  }

  Widget _sensorCard(
      String name, String unit, List<double> values, Color color) {
    if (values.isEmpty) return const SizedBox.shrink();

    final nonZero = values.where((v) => v != 0).toList();
    final max = values.reduce((a, b) => a > b ? a : b);
    final min =
        nonZero.isEmpty ? 0.0 : nonZero.reduce((a, b) => a < b ? a : b);
    final avg = values.reduce((a, b) => a + b) / values.length;

    // Downsample for sparkline: take evenly spaced points, max 200
    List<double> spark;
    if (values.length > 200) {
      final step = values.length / 200;
      spark = List.generate(200, (i) => values[(i * step).floor()]);
    } else {
      spark = values;
    }

    return Padding(
      padding: const EdgeInsets.only(bottom: 12),
      child: Container(
        padding: const EdgeInsets.all(14),
        decoration: BoxDecoration(
          color: Colors.grey[850],
          borderRadius: BorderRadius.circular(12),
        ),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Text(name,
                    style: TextStyle(
                        color: color,
                        fontSize: 14,
                        fontWeight: FontWeight.bold)),
                if (unit.isNotEmpty)
                  Text(' $unit',
                      style:
                          TextStyle(color: Colors.grey[600], fontSize: 11)),
                const Spacer(),
                Text('now: ${values.last.round()}',
                    style: TextStyle(color: color, fontSize: 13)),
              ],
            ),
            const SizedBox(height: 10),
            Row(
              children: [
                _statBox('MAX', max, color),
                _statBox('AVG', avg, Colors.white54),
                _statBox('MIN', min, Colors.white38),
              ],
            ),
            const SizedBox(height: 8),
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
          Text(label,
              style: const TextStyle(color: Colors.white24, fontSize: 9)),
          Text('${value.round()}',
              style: TextStyle(
                  color: color,
                  fontSize: 16,
                  fontWeight: FontWeight.bold,
                  fontFamily: 'monospace')),
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
