import 'dart:async';
import 'package:flutter/material.dart';
import '../models/telemetry.dart';
import '../services/ble_service.dart';

class RawLogScreen extends StatefulWidget {
  final BleService bleService;

  const RawLogScreen({super.key, required this.bleService});

  @override
  State<RawLogScreen> createState() => _RawLogScreenState();
}

class _RawLogScreenState extends State<RawLogScreen> {
  final List<_LogEntry> _entries = [];
  final ScrollController _scroll = ScrollController();
  StreamSubscription? _sub;
  bool _autoScroll = true;
  bool _paused = false;
  static const int _maxEntries = 500;

  @override
  void initState() {
    super.initState();
    _sub = widget.bleService.telemetryStream.listen(_onFrame);
  }

  void _onFrame(Telemetry t) {
    if (_paused) return;
    setState(() {
      _entries.add(_LogEntry(
        time: DateTime.now(),
        canId: t.rpm,
        dlc: t.speed,
        data: [t.throttle, t.coolantTemp, t.mapKpa, t.iat],
      ));
      if (_entries.length > _maxEntries) {
        _entries.removeRange(0, _entries.length - _maxEntries);
      }
    });
    if (_autoScroll) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (_scroll.hasClients) {
          _scroll.jumpTo(_scroll.position.maxScrollExtent);
        }
      });
    }
  }

  @override
  void dispose() {
    _sub?.cancel();
    _scroll.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        Container(
          padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
          color: Colors.grey[850],
          child: Row(
            children: [
              Text(
                '${_entries.length} frames',
                style: const TextStyle(color: Colors.white70, fontSize: 13),
              ),
              const Spacer(),
              IconButton(
                icon: Icon(
                  _paused ? Icons.play_arrow : Icons.pause,
                  color: Colors.white70,
                  size: 20,
                ),
                onPressed: () => setState(() => _paused = !_paused),
                tooltip: _paused ? 'Resume' : 'Pause',
              ),
              IconButton(
                icon: Icon(
                  Icons.vertical_align_bottom,
                  color: _autoScroll ? Colors.blue : Colors.white38,
                  size: 20,
                ),
                onPressed: () => setState(() => _autoScroll = !_autoScroll),
                tooltip: 'Auto-scroll',
              ),
              IconButton(
                icon: const Icon(Icons.delete_outline, color: Colors.white38, size: 20),
                onPressed: () => setState(() => _entries.clear()),
                tooltip: 'Clear',
              ),
            ],
          ),
        ),
        Container(
          padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
          color: Colors.grey[800],
          child: const Row(
            children: [
              SizedBox(width: 70, child: Text('TIME', style: _headerStyle)),
              SizedBox(width: 80, child: Text('CAN ID', style: _headerStyle)),
              SizedBox(width: 35, child: Text('DLC', style: _headerStyle)),
              Expanded(child: Text('DATA', style: _headerStyle)),
            ],
          ),
        ),
        Expanded(
          child: _entries.isEmpty
              ? Center(
                  child: Text(
                    'No CAN frames yet\nConnect to vehicle to see data',
                    textAlign: TextAlign.center,
                    style: TextStyle(color: Colors.grey[600], fontSize: 14),
                  ),
                )
              : ListView.builder(
                  controller: _scroll,
                  itemCount: _entries.length,
                  itemExtent: 28,
                  itemBuilder: (context, index) {
                    final e = _entries[index];
                    return Container(
                      padding: const EdgeInsets.symmetric(horizontal: 12),
                      color: index.isEven ? Colors.grey[900] : Colors.grey[870],
                      alignment: Alignment.centerLeft,
                      child: Row(
                        children: [
                          SizedBox(
                            width: 70,
                            child: Text(
                              '${e.time.hour.toString().padLeft(2, '0')}:'
                              '${e.time.minute.toString().padLeft(2, '0')}:'
                              '${e.time.second.toString().padLeft(2, '0')}.'
                              '${(e.time.millisecond ~/ 100)}',
                              style: _monoStyle,
                            ),
                          ),
                          SizedBox(
                            width: 80,
                            child: Text(
                              '0x${e.canId.toRadixString(16).toUpperCase().padLeft(3, '0')}',
                              style: _monoStyle.copyWith(color: Colors.amber),
                            ),
                          ),
                          SizedBox(
                            width: 35,
                            child: Text('${e.dlc}', style: _monoStyle),
                          ),
                          Expanded(
                            child: Text(
                              e.data
                                  .take(e.dlc)
                                  .map((b) => b.toRadixString(16).toUpperCase().padLeft(2, '0'))
                                  .join(' '),
                              style: _monoStyle.copyWith(color: Colors.lightGreenAccent),
                            ),
                          ),
                        ],
                      ),
                    );
                  },
                ),
        ),
      ],
    );
  }

  static const _headerStyle = TextStyle(
    color: Colors.white38,
    fontSize: 11,
    fontWeight: FontWeight.bold,
  );

  static const _monoStyle = TextStyle(
    color: Colors.white70,
    fontSize: 12,
    fontFamily: 'monospace',
  );
}

class _LogEntry {
  final DateTime time;
  final int canId;
  final int dlc;
  final List<int> data;

  _LogEntry({
    required this.time,
    required this.canId,
    required this.dlc,
    required this.data,
  });
}
