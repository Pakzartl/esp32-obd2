import 'dart:async';
import 'package:flutter/material.dart';
import '../models/telemetry.dart';
import '../services/ble_service.dart';
import '../services/database_service.dart';
import '../widgets/gauge_card.dart';
import 'scan_screen.dart';
import 'history_screen.dart';
import 'raw_log_screen.dart';

class DashboardScreen extends StatefulWidget {
  final BleService bleService;

  const DashboardScreen({super.key, required this.bleService});

  @override
  State<DashboardScreen> createState() => _DashboardScreenState();
}

class _DashboardScreenState extends State<DashboardScreen> {
  final _db = DatabaseService();
  Telemetry _current = Telemetry.empty();
  bool _connected = true;
  bool _recording = false;
  int _recordCount = 0;
  int _tabIndex = 0;
  StreamSubscription? _telemetrySub;
  StreamSubscription? _connectionSub;

  @override
  void initState() {
    super.initState();
    widget.bleService.startLiveNotify();
    _telemetrySub = widget.bleService.telemetryStream.listen((t) {
      setState(() => _current = t);
      if (_recording) {
        _db.insertTelemetry(t);
        setState(() => _recordCount++);
      }
    });
    _connectionSub = widget.bleService.connectionStream.listen((connected) {
      setState(() => _connected = connected);
      if (!connected && mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('Disconnected from device')),
        );
      }
    });
  }

  @override
  void dispose() {
    _telemetrySub?.cancel();
    _connectionSub?.cancel();
    super.dispose();
  }

  void _toggleRecording() {
    setState(() {
      _recording = !_recording;
      if (_recording) _recordCount = 0;
    });
  }

  Widget _buildGauges() {
    return Column(
      children: [
        Expanded(
          child: Padding(
            padding: const EdgeInsets.all(12),
            child: GridView.count(
              crossAxisCount: 2,
              mainAxisSpacing: 12,
              crossAxisSpacing: 12,
              childAspectRatio: 1.2,
              children: [
                GaugeCard(
                  label: 'CAN ID',
                  value: '0x${_current.rpm.toRadixString(16).toUpperCase()}',
                  unit: '',
                  icon: Icons.memory,
                  color: Colors.orange,
                ),
                GaugeCard(
                  label: 'DLC',
                  value: '${_current.speed}',
                  unit: 'bytes',
                  icon: Icons.data_usage,
                  color: Colors.blue,
                ),
                GaugeCard(
                  label: 'Byte 0',
                  value: '0x${_current.throttle.toRadixString(16).toUpperCase().padLeft(2, '0')}',
                  unit: '',
                  icon: Icons.grid_view,
                  color: Colors.green,
                ),
                GaugeCard(
                  label: 'Byte 1',
                  value: '0x${_current.coolantTemp.toRadixString(16).toUpperCase().padLeft(2, '0')}',
                  unit: '',
                  icon: Icons.grid_view,
                  color: Colors.cyan,
                ),
                GaugeCard(
                  label: 'Byte 2',
                  value: '0x${_current.gear.toRadixString(16).toUpperCase().padLeft(2, '0')}',
                  unit: '',
                  icon: Icons.grid_view,
                  color: Colors.purple,
                ),
                GaugeCard(
                  label: 'Byte 3',
                  value: '0x${_current.fuelLevel.toRadixString(16).toUpperCase().padLeft(2, '0')}',
                  unit: '',
                  icon: Icons.grid_view,
                  color: Colors.amber,
                ),
              ],
            ),
          ),
        ),
      ],
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Text(_tabIndex == 0 ? 'ADV350 Dashboard' : 'Raw CAN Log'),
        backgroundColor: Colors.black87,
        foregroundColor: Colors.white,
        actions: [
          IconButton(
            icon: const Icon(Icons.sensors),
            tooltip: 'Ping',
            onPressed: () async {
              final ok = await widget.bleService.ping();
              if (mounted) {
                ScaffoldMessenger.of(context).showSnackBar(
                  SnackBar(
                    content: Text(ok ? 'Ping OK — LED blink' : 'Ping failed'),
                    duration: const Duration(seconds: 1),
                  ),
                );
              }
            },
          ),
          IconButton(
            icon: const Icon(Icons.history),
            onPressed: () => Navigator.push(
              context,
              MaterialPageRoute(builder: (_) => const HistoryScreen()),
            ),
          ),
          IconButton(
            icon: const Icon(Icons.bluetooth_disabled),
            onPressed: () async {
              await widget.bleService.clearLastDevice();
              await widget.bleService.disconnect();
              if (mounted) {
                Navigator.pushReplacement(
                  context,
                  MaterialPageRoute(builder: (_) => const ScanScreen()),
                );
              }
            },
          ),
        ],
      ),
      backgroundColor: Colors.grey[900],
      body: Column(
        children: [
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
            color: _connected ? Colors.green[800] : Colors.red[800],
            child: Row(
              children: [
                Icon(
                  _connected ? Icons.bluetooth_connected : Icons.bluetooth_disabled,
                  color: Colors.white,
                  size: 16,
                ),
                const SizedBox(width: 8),
                Text(
                  _connected ? 'Connected' : 'Disconnected',
                  style: const TextStyle(color: Colors.white, fontSize: 13),
                ),
                const Spacer(),
                if (_recording)
                  Row(
                    children: [
                      const Icon(Icons.fiber_manual_record, color: Colors.red, size: 14),
                      const SizedBox(width: 4),
                      Text(
                        'REC $_recordCount',
                        style: const TextStyle(color: Colors.red, fontSize: 13),
                      ),
                    ],
                  ),
              ],
            ),
          ),
          Expanded(
            child: _tabIndex == 0
                ? _buildGauges()
                : RawLogScreen(bleService: widget.bleService),
          ),
        ],
      ),
      floatingActionButton: _tabIndex == 0
          ? FloatingActionButton.extended(
              onPressed: _toggleRecording,
              backgroundColor: _recording ? Colors.red : Colors.green,
              icon: Icon(_recording ? Icons.stop : Icons.fiber_manual_record),
              label: Text(_recording ? 'Stop' : 'Record'),
            )
          : null,
      bottomNavigationBar: BottomNavigationBar(
        currentIndex: _tabIndex,
        onTap: (i) => setState(() => _tabIndex = i),
        backgroundColor: Colors.black87,
        selectedItemColor: Colors.blue,
        unselectedItemColor: Colors.white38,
        items: const [
          BottomNavigationBarItem(
            icon: Icon(Icons.dashboard),
            label: 'Dashboard',
          ),
          BottomNavigationBarItem(
            icon: Icon(Icons.terminal),
            label: 'Raw Log',
          ),
        ],
      ),
    );
  }
}
