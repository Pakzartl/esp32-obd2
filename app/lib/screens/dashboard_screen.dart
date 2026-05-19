import 'dart:async';
import 'package:flutter/material.dart';
import '../models/telemetry.dart';
import '../services/ble_service.dart';
import '../services/database_service.dart';
import '../widgets/gauge_card.dart';
import 'scan_screen.dart';
import 'history_screen.dart';
import 'raw_log_screen.dart';
import 'metrics_screen.dart';
import 'ota_screen.dart';

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
  int _savedCount = 0;
  int _tabIndex = 0;
  StreamSubscription? _telemetrySub;
  StreamSubscription? _connectionSub;
  DateTime? _lastSave;

  @override
  void initState() {
    super.initState();
    widget.bleService.startLiveNotify();
    _telemetrySub = widget.bleService.telemetryStream.listen((t) {
      setState(() => _current = t);
      // Auto-save at 1 Hz
      final now = DateTime.now();
      if (_lastSave == null || now.difference(_lastSave!).inMilliseconds >= 1000) {
        if (t.rpm > 0 || t.speed > 0 || t.coolantTemp > 0) {
          _db.insertTelemetry(t);
          _savedCount++;
          _lastSave = now;
        }
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

  Widget _buildGauges() {
    return Column(
      children: [
        // Hero speed
        Container(
          width: double.infinity,
          padding: const EdgeInsets.symmetric(vertical: 16),
          child: Column(
            children: [
              Text(
                '${_current.speed}',
                style: const TextStyle(
                  color: Colors.white,
                  fontSize: 72,
                  fontWeight: FontWeight.bold,
                  fontFamily: 'monospace',
                ),
              ),
              const Text(
                'km/h',
                style: TextStyle(color: Colors.white54, fontSize: 16),
              ),
            ],
          ),
        ),
        Expanded(
          child: Padding(
            padding: const EdgeInsets.all(8),
            child: GridView.count(
              crossAxisCount: 2,
              mainAxisSpacing: 8,
              crossAxisSpacing: 8,
              childAspectRatio: 1.5,
              children: [
                GaugeCard(
                  label: 'RPM',
                  value: '${_current.rpm}',
                  unit: '',
                  icon: Icons.speed,
                  color: _current.rpm > 7000 ? Colors.red : Colors.orange,
                ),
                GaugeCard(
                  label: 'Throttle',
                  value: '${_current.throttle}',
                  unit: '%',
                  icon: Icons.flash_on,
                  color: Colors.green,
                ),
                GaugeCard(
                  label: 'Coolant',
                  value: '${_current.coolantTemp}',
                  unit: '°C',
                  icon: Icons.thermostat,
                  color: _current.coolantTemp > 105
                      ? Colors.red
                      : _current.coolantTemp > 90
                          ? Colors.orange
                          : Colors.blue,
                ),
                GaugeCard(
                  label: 'MAP',
                  value: '${_current.mapKpa}',
                  unit: 'kPa',
                  icon: Icons.compress,
                  color: Colors.purple,
                ),
                GaugeCard(
                  label: 'IAT',
                  value: '${_current.iat}',
                  unit: '°C',
                  icon: Icons.air,
                  color: Colors.cyan,
                ),
                GaugeCard(
                  label: 'Load',
                  value: '${_current.engineLoad}',
                  unit: '%',
                  icon: Icons.engineering,
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
            icon: const Icon(Icons.system_update),
            tooltip: 'Firmware Update',
            onPressed: () => Navigator.push(
              context,
              MaterialPageRoute(
                builder: (_) => OtaScreen(bleService: widget.bleService),
              ),
            ),
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
                if (_savedCount > 0)
                  Row(
                    children: [
                      const Icon(Icons.save, color: Colors.white38, size: 14),
                      const SizedBox(width: 4),
                      Text(
                        '$_savedCount saved',
                        style: const TextStyle(color: Colors.white38, fontSize: 12),
                      ),
                    ],
                  ),
              ],
            ),
          ),
          Expanded(
            child: _tabIndex == 0
                ? _buildGauges()
                : _tabIndex == 1
                    ? const MetricsScreen()
                    : RawLogScreen(bleService: widget.bleService),
          ),
        ],
      ),
      floatingActionButton: null,
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
            icon: Icon(Icons.analytics),
            label: 'Metrics',
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
