import 'package:flutter/material.dart';
import 'screens/scan_screen.dart';
import 'screens/dashboard_screen.dart';
import 'services/ble_service.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
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
          brightness: Brightness.light,
        ),
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
      _goScan();
      return;
    }

    for (int attempt = 0; attempt < 3; attempt++) {
      if (!mounted) return;
      if (attempt > 0) await Future.delayed(const Duration(seconds: 2));
      final results = await _bleService.scan(timeout: const Duration(seconds: 5));
      final match = results.where((r) => r.device.remoteId.str == lastId);
      if (match.isNotEmpty && mounted) {
        try {
          await _bleService.connect(match.first.device);
          if (mounted) {
            Navigator.pushReplacement(
              context,
              MaterialPageRoute(
                builder: (_) => DashboardScreen(bleService: _bleService),
              ),
            );
            return;
          }
        } catch (_) {}
      }
    }
    _goScan();
  }

  void _goScan() {
    if (!mounted) return;
    Navigator.pushReplacement(
      context,
      MaterialPageRoute(builder: (_) => const ScanScreen()),
    );
  }

  void _goDemo() {
    if (!mounted) return;
    Navigator.pushReplacement(
      context,
      MaterialPageRoute(
        builder: (_) => DashboardScreen(bleService: _bleService),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.grey[900],
      body: Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            const CircularProgressIndicator(color: Colors.blue),
            const SizedBox(height: 16),
            const Text(
              'Connecting to ADV350...',
              style: TextStyle(color: Colors.white70, fontSize: 16),
            ),
            const SizedBox(height: 32),
            TextButton(
              onPressed: _goDemo,
              child: const Text(
                'Skip — Demo Mode',
                style: TextStyle(color: Colors.white38, fontSize: 14),
              ),
            ),
          ],
        ),
      ),
    );
  }
}
