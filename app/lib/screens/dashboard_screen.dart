import 'dart:async';
import 'package:flutter/material.dart';
import '../models/telemetry.dart';
import '../services/ble_service.dart';
import '../services/database_service.dart';
import '../services/raw_backup_service.dart';
import '../services/cloud_sync_service.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'settings_screen.dart';
import 'tabs/ride_tab.dart';
import 'tabs/trip_tab.dart';
import 'tabs/vehicle_tab.dart';
import 'tabs/dev_tab.dart';

class DashboardScreen extends StatefulWidget {
  final BleService bleService;

  const DashboardScreen({super.key, required this.bleService});

  @override
  State<DashboardScreen> createState() => _DashboardScreenState();
}

class _DashboardScreenState extends State<DashboardScreen> {
  final _db = DatabaseService();
  final _rawBackup = RawBackupService();
  late final CloudSyncService _cloudSync = CloudSyncService(_db);
  Telemetry _current = Telemetry.empty();
  BleState _bleState = BleState.connecting;
  bool _skipBle = false;
  int _savedCount = 0;
  int _rawCount = 0;
  int _tabIndex = 0;
  StreamSubscription? _telemetrySub;
  StreamSubscription? _rawSub;
  StreamSubscription? _connectionSub;
  DateTime? _lastSave;
  DateTime? _lastUiUpdate;

  @override
  void initState() {
    super.initState();
    _initBackupAndStart();
  }

  Future<void> _initBackupAndStart() async {
    _skipBle = await SettingsScreen.isSkipBle();
    if (_skipBle) {
      setState(() => _bleState = BleState.disconnected);
      return;
    }
    await _rawBackup.startSession();
    final prefs = await SharedPreferences.getInstance();
    final apiKey = prefs.getString('cloud_api_key') ?? '';
    if (apiKey.isNotEmpty) {
      _cloudSync.configure(apiKey: apiKey);
      _cloudSync.startPeriodic();
    }
    widget.bleService.startLiveNotify();

    _rawSub = widget.bleService.rawDataStream.listen((data) {
      _rawBackup.writeRaw(data);
      _rawCount = _rawBackup.count;
    });

    _telemetrySub = widget.bleService.telemetryStream.listen((t) {
      _current = t;
      final now = DateTime.now();
      // Throttle UI to ~4Hz
      if (_lastUiUpdate == null || now.difference(_lastUiUpdate!).inMilliseconds >= 250) {
        _lastUiUpdate = now;
        setState(() {});
      }
      // Save at 2Hz
      if (_lastSave == null || now.difference(_lastSave!).inMilliseconds >= 500) {
        if (t.rpm > 0 || t.speed > 0 || t.coolantTemp > 0) {
          _db.insertTelemetry(t);
          _savedCount++;
          _lastSave = now;
        }
      }
    });
    _connectionSub = widget.bleService.connectionStream.listen((connected) {
      setState(() => _bleState = connected
          ? BleState.connected
          : (_bleState == BleState.connected ? BleState.disconnected : _bleState));
      if (connected) {
        widget.bleService.startLiveNotify();
      } else if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('Disconnected — reconnecting...')),
        );
      }
    });
  }

  @override
  void dispose() {
    _telemetrySub?.cancel();
    _rawSub?.cancel();
    _connectionSub?.cancel();
    _rawBackup.endSession();
    _cloudSync.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.grey[900],
      body: SafeArea(
        child: IndexedStack(
          index: _tabIndex,
          children: [
            RideTab(
              current: _current,
              bleState: _bleState,
            ),
            TripTab(current: _current, isActive: _tabIndex == 1),
            VehicleTab(current: _current),
            DevTab(
              bleService: widget.bleService,
              cloudSync: _cloudSync,
              connected: _bleState == BleState.connected,
              skipBle: _skipBle,
              savedCount: _savedCount,
              rawCount: _rawCount,
            ),
          ],
        ),
      ),
      bottomNavigationBar: NavigationBar(
        selectedIndex: _tabIndex,
        onDestinationSelected: (i) => setState(() => _tabIndex = i),
        backgroundColor: Colors.black,
        indicatorColor: Colors.blue.withValues(alpha: 0.2),
        height: 64,
        labelBehavior: NavigationDestinationLabelBehavior.alwaysShow,
        destinations: const [
          NavigationDestination(
            icon: Icon(Icons.motorcycle, color: Colors.white38),
            selectedIcon: Icon(Icons.motorcycle, color: Colors.blue),
            label: 'Ride',
          ),
          NavigationDestination(
            icon: Icon(Icons.analytics_outlined, color: Colors.white38),
            selectedIcon: Icon(Icons.analytics, color: Colors.blue),
            label: 'Trip',
          ),
          NavigationDestination(
            icon: Icon(Icons.directions_car_outlined, color: Colors.white38),
            selectedIcon: Icon(Icons.directions_car, color: Colors.blue),
            label: 'Vehicle',
          ),
          NavigationDestination(
            icon: Icon(Icons.developer_mode_outlined, color: Colors.white38),
            selectedIcon: Icon(Icons.developer_mode, color: Colors.blue),
            label: 'Dev',
          ),
        ],
      ),
    );
  }
}
