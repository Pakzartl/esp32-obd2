import 'package:flutter/material.dart';
import 'screens/scan_screen.dart';

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
      home: const ScanScreen(),
    );
  }
}
