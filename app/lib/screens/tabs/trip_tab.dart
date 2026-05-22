import 'dart:async';
import 'package:flutter/material.dart';
import '../../models/telemetry.dart';
import '../../services/database_service.dart';

class TripTab extends StatefulWidget {
  final Telemetry current;
  final bool isActive;

  const TripTab({super.key, required this.current, this.isActive = true});

  @override
  State<TripTab> createState() => _TripTabState();
}

class _TripTabState extends State<TripTab> {
  final _db = DatabaseService();
  List<Telemetry> _data = [];
  bool _loading = true;
  bool _loadInProgress = false;
  String _range = '30';
  String? _prevRange;
  DateTime? _customStart;
  DateTime? _customEnd;
  Timer? _refreshTimer;
  DateTime? _lastTimestamp;
  int _dataVersion = 0;

  double? _selStartFrac;
  double? _selEndFrac;
  bool _isDragging = false;

  @override
  void initState() {
    super.initState();
    _load();
    _refreshTimer = Timer.periodic(const Duration(seconds: 5), (_) {
      if (widget.isActive && _range != 'all' && _range != 'custom' && !_loadInProgress) {
        _load(silent: true);
      }
    });
  }

  @override
  void dispose() {
    _refreshTimer?.cancel();
    super.dispose();
  }

  Future<void> _load({bool silent = false}) async {
    if (_loadInProgress) return;
    _loadInProgress = true;

    // Incremental: only fetch new rows if we have data and it's a silent refresh
    if (silent && _data.isNotEmpty && _lastTimestamp != null) {
      final newRows = await _db.getAfter(_lastTimestamp!);
      if (newRows.isEmpty) {
        // Trim old rows outside range window
        _trimOldData();
        _loadInProgress = false;
        return;
      }
      final filtered = newRows.where((t) => t.rpm < 20000).toList();
      _data.addAll(filtered);
      _trimOldData();
      if (_data.isNotEmpty) _lastTimestamp = _data.last.timestamp;
      _dataVersion++;
      _loadInProgress = false;
      if (mounted) setState(() {});
      return;
    }

    // Full load: first load, range change, zoom
    if (!silent && mounted) setState(() => _loading = true);
    final limit = _range == 'all' ? 10000 : 2000;
    final all = await _db.getRecentForTrip(limit: limit);
    var decoded = all.where((t) => t.rpm < 20000).toList();

    if (_range == 'custom' && _customStart != null && _customEnd != null) {
      decoded = decoded
          .where((t) =>
              t.timestamp.isAfter(_customStart!) &&
              t.timestamp.isBefore(_customEnd!))
          .toList();
    } else if (_range != 'all') {
      final mins = int.tryParse(_range) ?? 9999;
      final cutoff = DateTime.now().subtract(Duration(minutes: mins));
      decoded = decoded.where((t) => t.timestamp.isAfter(cutoff)).toList();
    }
    _data = decoded.reversed.toList();
    _lastTimestamp = _data.isNotEmpty ? _data.last.timestamp : null;
    _dataVersion++;
    _loadInProgress = false;
    if (mounted) setState(() => _loading = false);
  }

  void _trimOldData() {
    if (_range == 'all') {
      if (_data.length > 10000) _data = _data.sublist(_data.length - 10000);
      return;
    }
    if (_range == 'custom') return;
    final mins = int.tryParse(_range) ?? 9999;
    final cutoff = DateTime.now().subtract(Duration(minutes: mins));
    _data.removeWhere((t) => t.timestamp.isBefore(cutoff));
  }

  void _applySelection() {
    if (_data.isEmpty || _selStartFrac == null || _selEndFrac == null) return;
    final lo = _selStartFrac! < _selEndFrac! ? _selStartFrac! : _selEndFrac!;
    final hi = _selStartFrac! > _selEndFrac! ? _selStartFrac! : _selEndFrac!;
    if ((hi - lo) < 0.02) return;

    final tMin = _data.first.timestamp.millisecondsSinceEpoch;
    final tMax = _data.last.timestamp.millisecondsSinceEpoch;
    final tRange = tMax - tMin;
    if (tRange == 0) return;

    _prevRange ??= _range;
    _customStart = DateTime.fromMillisecondsSinceEpoch((tMin + lo * tRange).round());
    _customEnd = DateTime.fromMillisecondsSinceEpoch((tMin + hi * tRange).round());
    _range = 'custom';
    _selStartFrac = null;
    _selEndFrac = null;
    _load();
  }

  void _resetZoom() {
    _range = _prevRange ?? '30';
    _prevRange = null;
    _customStart = null;
    _customEnd = null;
    _selStartFrac = null;
    _selEndFrac = null;
    _load();
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
              primary: Colors.blue, surface: Colors.grey[900]!),
        ),
        child: child!,
      ),
    );
    if (dateRange == null) return;

    final startTime = await showTimePicker(
      context: context,
      initialTime:
          TimeOfDay.fromDateTime(_customStart ?? dateRange.start),
      builder: (context, child) => Theme(
        data: Theme.of(context).copyWith(
          colorScheme: ColorScheme.dark(
              primary: Colors.blue, surface: Colors.grey[900]!),
        ),
        child: child!,
      ),
    );
    if (startTime == null) return;

    final endTime = await showTimePicker(
      context: context,
      initialTime: TimeOfDay.fromDateTime(_customEnd ?? dateRange.end),
      builder: (context, child) => Theme(
        data: Theme.of(context).copyWith(
          colorScheme: ColorScheme.dark(
              primary: Colors.blue, surface: Colors.grey[900]!),
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
    _prevRange = null;
    _load();
  }

  @override
  Widget build(BuildContext context) {
    return _loading
        ? const Center(child: CircularProgressIndicator(color: Colors.blue))
        : ListView(
            padding: const EdgeInsets.all(16),
            children: [
              _buildRangeChips(),
              const SizedBox(height: 16),
              _buildTripSummary(),
              const SizedBox(height: 16),
              if (_data.isNotEmpty) ...[
                _sensorCard('Speed', 'km/h', _data,
                    (t) => t.speed.toDouble(), Colors.blue),
                _sensorCard('RPM', '', _data,
                    (t) => t.rpm.toDouble(), Colors.orange),
                _sensorCard('Throttle', '%', _data,
                    (t) => t.throttle.toDouble(), Colors.green),
                _sensorCard('Coolant', '°C', _data,
                    (t) => t.coolantTemp.toDouble(), Colors.red),
                _sensorCard('MAP', 'kPa', _data,
                    (t) => t.mapKpa.toDouble(), Colors.purple),
                _sensorCard('IAT', '°C', _data,
                    (t) => t.iat.toDouble(), Colors.teal),
                _sensorCard('Fuel Rate', 'L/h', _data,
                    (t) => t.fuelRateLph, Colors.orange),
                _sensorCard('CVT Ratio', '', _data,
                    (t) => t.cvtRatio, Colors.indigo),
                _sensorCard('Ride Score', '/100', _data,
                    (t) => t.ridingScore.toDouble(), Colors.greenAccent),
              ],
              if (_data.isEmpty)
                Padding(
                  padding: const EdgeInsets.only(top: 80),
                  child: Center(
                    child: Text(
                      'No trip data yet\nConnect and ride to see stats',
                      textAlign: TextAlign.center,
                      style:
                          TextStyle(color: Colors.grey[600], fontSize: 15),
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
            if (_prevRange != null)
              ActionChip(
                avatar: const Icon(Icons.zoom_out, size: 14, color: Colors.white),
                label: const Text('Reset zoom',
                    style: TextStyle(fontSize: 12, color: Colors.white)),
                backgroundColor: Colors.red[700],
                onPressed: _resetZoom,
                visualDensity: VisualDensity.compact,
              ),
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
            '${_data.length} pts  •  drag on graph to zoom',
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
        _prevRange = null;
        _load();
      },
      visualDensity: VisualDensity.compact,
    );
  }

  Widget _chipCustom() {
    final selected = _range == 'custom' && _prevRange == null;
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

    final speeds = _data.map((t) => t.speed.toDouble()).toList();
    final avgSpeed =
        speeds.isEmpty ? 0.0 : speeds.reduce((a, b) => a + b) / speeds.length;
    final maxSpeed =
        speeds.isEmpty ? 0.0 : speeds.reduce((a, b) => a > b ? a : b);

    double distKm = 0;
    for (int i = 1; i < _data.length; i++) {
      final dtSec = _data[i].timestamp.difference(_data[i - 1].timestamp).inMilliseconds / 1000.0;
      if (dtSec > 0 && dtSec < 10) {
        distKm += _data[i].speed * dtSec / 3600;
      }
    }

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
        Row(children: [
          Expanded(child: _summaryCard(Icons.timer_outlined, 'Duration',
              '${mins}m ${secs}s', Colors.blue)),
          const SizedBox(width: 12),
          Expanded(child: _summaryCard(Icons.route, 'Distance',
              '${distKm.toStringAsFixed(1)} km', Colors.green)),
        ]),
        const SizedBox(height: 12),
        Row(children: [
          Expanded(child: _summaryCard(Icons.speed, 'Avg Speed',
              '${avgSpeed.round()} km/h', Colors.cyan)),
          const SizedBox(width: 12),
          Expanded(child: _summaryCard(Icons.star_outline, 'Ride Score',
              '${smoothness.round()}', _scoreColor(smoothness))),
        ]),
        const SizedBox(height: 12),
        Row(children: [
          Expanded(child: _summaryCard(Icons.trending_up, 'Max Speed',
              '${maxSpeed.round()} km/h', Colors.orange)),
          const SizedBox(width: 12),
          Expanded(child: _summaryCard(Icons.data_usage, 'Samples',
              '${_data.length}', Colors.purple)),
        ]),
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
          Row(children: [
            Icon(icon, color: color.withValues(alpha: 0.7), size: 16),
            const SizedBox(width: 6),
            Text(label,
                style: TextStyle(color: Colors.grey[500], fontSize: 11)),
          ]),
          const SizedBox(height: 8),
          Text(value,
              style: TextStyle(
                  color: color, fontSize: 20,
                  fontWeight: FontWeight.bold, fontFamily: 'monospace')),
        ],
      ),
    );
  }

  Widget _sensorCard(String name, String unit, List<Telemetry> data,
      double Function(Telemetry) getValue, Color color) {
    if (data.isEmpty) return const SizedBox.shrink();

    final values = data.map(getValue).toList();
    final timestamps =
        data.map((t) => t.timestamp.millisecondsSinceEpoch).toList();

    final nonZero = values.where((v) => v != 0).toList();
    final max = values.reduce((a, b) => a > b ? a : b);
    final min =
        nonZero.isEmpty ? 0.0 : nonZero.reduce((a, b) => a < b ? a : b);
    final avg = values.reduce((a, b) => a + b) / values.length;

    List<double> spark;
    List<int> sparkTs;
    if (values.length > 300) {
      final step = values.length / 300;
      spark = List.generate(300, (i) => values[(i * step).floor()]);
      sparkTs = List.generate(300, (i) => timestamps[(i * step).floor()]);
    } else {
      spark = values;
      sparkTs = timestamps;
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
            Row(children: [
              Text(name,
                  style: TextStyle(
                      color: color, fontSize: 14, fontWeight: FontWeight.bold)),
              if (unit.isNotEmpty)
                Text(' $unit',
                    style: TextStyle(color: Colors.grey[600], fontSize: 11)),
              const Spacer(),
              Text('now: ${values.last.round()}',
                  style: TextStyle(color: color, fontSize: 13)),
            ]),
            const SizedBox(height: 10),
            Row(children: [
              _statBox('MAX', max, color),
              _statBox('AVG', avg, Colors.white54),
              _statBox('MIN', min, Colors.white38),
            ]),
            const SizedBox(height: 8),
            GestureDetector(
              onHorizontalDragStart: (d) {
                final frac = (d.localPosition.dx /
                        context.findRenderObject()!.paintBounds.width)
                    .clamp(0.0, 1.0);
                setState(() {
                  _isDragging = true;
                  _selStartFrac = frac;
                  _selEndFrac = frac;
                });
              },
              onHorizontalDragUpdate: (d) {
                final box = context.findRenderObject() as RenderBox;
                final frac =
                    (d.localPosition.dx / box.size.width).clamp(0.0, 1.0);
                setState(() => _selEndFrac = frac);
              },
              onHorizontalDragEnd: (_) {
                _isDragging = false;
                _applySelection();
              },
              child: SizedBox(
                height: 50,
                child: CustomPaint(
                  size: const Size(double.infinity, 50),
                  painter: _SparkPainter(
                    spark, sparkTs, color, _dataVersion,
                    selStart: _isDragging ? _selStartFrac : null,
                    selEnd: _isDragging ? _selEndFrac : null,
                  ),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _statBox(String label, double value, Color color) {
    return Expanded(
      child: Column(children: [
        Text(label,
            style: const TextStyle(color: Colors.white24, fontSize: 9)),
        Text('${value.round()}',
            style: TextStyle(
                color: color, fontSize: 16,
                fontWeight: FontWeight.bold, fontFamily: 'monospace')),
      ]),
    );
  }
}

class _SparkPainter extends CustomPainter {
  final List<double> values;
  final List<int> timestamps;
  final Color color;
  final int dataVersion;
  final double? selStart;
  final double? selEnd;
  static const int gapMs = 10000;

  _SparkPainter(this.values, this.timestamps, this.color, this.dataVersion,
      {this.selStart, this.selEnd});

  @override
  void paint(Canvas canvas, Size size) {
    if (values.length < 2 || timestamps.length != values.length) return;

    final vMax = values.reduce((a, b) => a > b ? a : b);
    final vMin = values.reduce((a, b) => a < b ? a : b);
    final vRange = vMax - vMin == 0 ? 1.0 : vMax - vMin;

    final tMin = timestamps.first;
    final tMax = timestamps.last;
    final tRange = tMax - tMin;
    if (tRange == 0) return;

    double toX(int ts) => (ts - tMin) / tRange * size.width;
    double toY(double v) =>
        size.height - ((v - vMin) / vRange).clamp(0, 1) * size.height;
    final baseline = toY(vMin);

    // Selection highlight
    if (selStart != null && selEnd != null) {
      final lo = selStart! < selEnd! ? selStart! : selEnd!;
      final hi = selStart! > selEnd! ? selStart! : selEnd!;
      canvas.drawRect(
        Rect.fromLTRB(
            lo * size.width, 0, hi * size.width, size.height),
        Paint()..color = Colors.blue.withValues(alpha: 0.2),
      );
      canvas.drawLine(
        Offset(lo * size.width, 0),
        Offset(lo * size.width, size.height),
        Paint()
          ..color = Colors.blue.withValues(alpha: 0.6)
          ..strokeWidth = 1,
      );
      canvas.drawLine(
        Offset(hi * size.width, 0),
        Offset(hi * size.width, size.height),
        Paint()
          ..color = Colors.blue.withValues(alpha: 0.6)
          ..strokeWidth = 1,
      );
    }

    // Data line
    final paint = Paint()
      ..color = color.withValues(alpha: 0.6)
      ..strokeWidth = 1.5
      ..style = PaintingStyle.stroke;

    final path = Path();
    bool first = true;
    for (int i = 0; i < values.length; i++) {
      final x = toX(timestamps[i]);
      final y = toY(values[i]);

      if (first) {
        path.moveTo(x, y);
        first = false;
      } else if (timestamps[i] - timestamps[i - 1] > gapMs) {
        final xPrev = toX(timestamps[i - 1]);
        path.lineTo(xPrev, baseline);
        path.lineTo(x, baseline);
        path.lineTo(x, y);
      } else {
        path.lineTo(x, y);
      }
    }
    canvas.drawPath(path, paint);
  }

  @override
  @override
  bool shouldRepaint(covariant _SparkPainter old) =>
      dataVersion != old.dataVersion ||
      selStart != old.selStart ||
      selEnd != old.selEnd;
}
