import 'dart:async';
import 'package:flutter/material.dart';
import '../models/telemetry.dart';
import '../services/ble_service.dart';
import '../services/database_service.dart';
import '../widgets/gauge_card.dart';
import 'scan_screen.dart';
import 'history_screen.dart';

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
  StreamSubscription? _telemetrySub;
  StreamSubscription? _connectionSub;

  @override
  void initState() {
    super.initState();
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

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('ADV350 Dashboard'),
        backgroundColor: Colors.black87,
        foregroundColor: Colors.white,
        actions: [
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
            child: Padding(
              padding: const EdgeInsets.all(12),
              child: GridView.count(
                crossAxisCount: 2,
                mainAxisSpacing: 12,
                crossAxisSpacing: 12,
                childAspectRatio: 1.2,
                children: [
                  GaugeCard(
                    label: 'RPM',
                    value: '${_current.rpm}',
                    unit: 'rpm',
                    icon: Icons.speed,
                    color: Colors.orange,
                  ),
                  GaugeCard(
                    label: 'Speed',
                    value: '${_current.speed}',
                    unit: 'km/h',
                    icon: Icons.motorcycle,
                    color: Colors.blue,
                  ),
                  GaugeCard(
                    label: 'Throttle',
                    value: '${_current.throttle}',
                    unit: '%',
                    icon: Icons.gas_meter,
                    color: Colors.green,
                  ),
                  GaugeCard(
                    label: 'Coolant',
                    value: '${_current.coolantTemp}',
                    unit: '°C',
                    icon: Icons.thermostat,
                    color: _current.coolantTemp > 100 ? Colors.red : Colors.cyan,
                  ),
                  GaugeCard(
                    label: 'Gear',
                    value: _current.gear == 0 ? 'N' : '${_current.gear}',
                    unit: '',
                    icon: Icons.settings,
                    color: Colors.purple,
                  ),
                  GaugeCard(
                    label: 'Fuel',
                    value: '${_current.fuelLevel}',
                    unit: '%',
                    icon: Icons.local_gas_station,
                    color: _current.fuelLevel < 20 ? Colors.red : Colors.amber,
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
      floatingActionButton: FloatingActionButton.extended(
        onPressed: _toggleRecording,
        backgroundColor: _recording ? Colors.red : Colors.green,
        icon: Icon(_recording ? Icons.stop : Icons.fiber_manual_record),
        label: Text(_recording ? 'Stop' : 'Record'),
      ),
    );
  }
}
