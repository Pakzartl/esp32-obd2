import 'package:flutter/material.dart';
import 'screens/scan_screen.dart';
import 'screens/dashboard_screen.dart';
import 'screens/settings_screen.dart';
import 'services/ble_service.dart';
import 'services/debug_server.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  if (await SettingsScreen.isDevMode()) {
    DebugServer().start();
  }
  runApp(const Adv350App());
}

class Adv350App extends StatelessWidget {
  const Adv350App({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'ADV350 Logger',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(
          seedColor: Colors.blue,
          brightness: Brightness.dark,
        ),
        scaffoldBackgroundColor: Colors.grey[900],
        useMaterial3: true,
      ),
      home: const StartupScreen(),
    );
  }
}

class StartupScreen extends StatefulWidget {
  const StartupScreen({super.key});

  @override
  State<StartupScreen> createState() => _StartupScreenState();
}

class _StartupScreenState extends State<StartupScreen> {
  final _bleService = BleService();

  @override
  void initState() {
    super.initState();
    _init();
  }

  Future<void> _init() async {
    final lastId = await _bleService.lastDeviceId;
    if (lastId == null) {
      // First time — onboard pair
      if (mounted) {
        Navigator.pushReplacement(
          context,
          MaterialPageRoute(builder: (_) => const ScanScreen()),
        );
      }
      return;
    }

    // Go to Ride immediately, auto-connect BLE in background
    if (mounted) {
      Navigator.pushReplacement(
        context,
        MaterialPageRoute(
          builder: (_) => DashboardScreen(bleService: _bleService),
        ),
      );
    }
    _autoConnect(lastId);
  }

  Future<void> _autoConnect(String lastId) async {
    for (int attempt = 0; attempt < 3; attempt++) {
      if (attempt > 0) await Future.delayed(const Duration(seconds: 2));
      try {
        final results = await _bleService.scan(timeout: const Duration(seconds: 5));
        final match = results.where((r) => r.device.remoteId.str == lastId);
        if (match.isNotEmpty) {
          await _bleService.connect(match.first.device);
          return;
        }
      } catch (_) {}
    }
  }

  @override
  Widget build(BuildContext context) {
    return const Scaffold(backgroundColor: Color(0xFF212121));
  }
}
