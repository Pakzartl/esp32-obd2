import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../services/debug_server.dart';
import '../services/raw_backup_service.dart';

class SettingsScreen extends StatefulWidget {
  const SettingsScreen({super.key});

  static const String devModeKey = 'dev_mode';
  static const String skipBleKey = 'skip_ble';

  static Future<bool> isDevMode() async {
    final prefs = await SharedPreferences.getInstance();
    return prefs.getBool(devModeKey) ?? false;
  }

  static Future<bool> isSkipBle() async {
    final prefs = await SharedPreferences.getInstance();
    return prefs.getBool(skipBleKey) ?? false;
  }

  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen> {
  bool _devMode = false;
  bool _skipBle = false;
  final _debugServer = DebugServer();
  final _rawBackup = RawBackupService();
  List<Map<String, dynamic>> _backupFiles = [];

  @override
  void initState() {
    super.initState();
    _loadSettings();
  }

  Future<void> _loadSettings() async {
    final prefs = await SharedPreferences.getInstance();
    final files = await _rawBackup.listBackups();
    final fileInfos = <Map<String, dynamic>>[];
    for (final f in files) {
      final stat = await f.stat();
      fileInfos.add({
        'name': f.path.split('/').last,
        'size': stat.size,
        'modified': stat.modified,
      });
    }
    setState(() {
      _devMode = prefs.getBool(SettingsScreen.devModeKey) ?? false;
      _skipBle = prefs.getBool(SettingsScreen.skipBleKey) ?? false;
      _backupFiles = fileInfos;
    });
  }

  Future<void> _toggleDevMode(bool value) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setBool(SettingsScreen.devModeKey, value);
    if (value) {
      await _debugServer.start();
    } else {
      await _debugServer.stop();
    }
    setState(() => _devMode = value);
  }

  Future<void> _toggleSkipBle(bool value) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setBool(SettingsScreen.skipBleKey, value);
    setState(() => _skipBle = value);
  }

  String _formatSize(int bytes) {
    if (bytes < 1024) return '$bytes B';
    if (bytes < 1024 * 1024) return '${(bytes / 1024).toStringAsFixed(1)} KB';
    return '${(bytes / 1024 / 1024).toStringAsFixed(1)} MB';
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Settings'),
        backgroundColor: Colors.black87,
        foregroundColor: Colors.white,
      ),
      backgroundColor: Colors.grey[900],
      body: ListView(
        children: [
          _sectionHeader('General'),
          SwitchListTile(
            title: const Text('Skip Bluetooth', style: TextStyle(color: Colors.white)),
            subtitle: const Text(
              'Go to dashboard without BLE connection',
              style: TextStyle(color: Colors.white38, fontSize: 12),
            ),
            value: _skipBle,
            onChanged: _toggleSkipBle,
            secondary: const Icon(Icons.bluetooth_disabled, color: Colors.white54),
          ),
          const Divider(color: Colors.white12),
          _sectionHeader('Developer'),
          SwitchListTile(
            title: const Text('Developer Mode', style: TextStyle(color: Colors.white)),
            subtitle: Text(
              _devMode ? 'Debug server on port 8350' : 'Enable debug HTTP server',
              style: const TextStyle(color: Colors.white38, fontSize: 12),
            ),
            value: _devMode,
            onChanged: _toggleDevMode,
            secondary: Icon(
              Icons.developer_mode,
              color: _devMode ? Colors.greenAccent : Colors.white54,
            ),
          ),
          if (_devMode) ...[
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
              child: Card(
                color: Colors.grey[850],
                child: Padding(
                  padding: const EdgeInsets.all(12),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      const Text('Debug Server', style: TextStyle(color: Colors.greenAccent, fontWeight: FontWeight.bold)),
                      const SizedBox(height: 8),
                      _codeRow('adb forward tcp:8350 tcp:8350'),
                      const SizedBox(height: 4),
                      _codeRow('curl localhost:8350/db/count'),
                      _codeRow('curl localhost:8350/db/recent?limit=5'),
                      _codeRow('curl localhost:8350/db/schema'),
                      _codeRow('curl localhost:8350/raw/list'),
                      _codeRow('curl -o backup.jsonl localhost:8350/raw/pull/<file>'),
                      _codeRow('curl -o adv350.db localhost:8350/db/export'),
                      const SizedBox(height: 6),
                      const Text('SQL Console', style: TextStyle(color: Colors.orangeAccent, fontWeight: FontWeight.bold, fontSize: 12)),
                      const SizedBox(height: 4),
                      _codeRow('curl "localhost:8350/db/sql?q=SELECT+*+FROM+telemetry+LIMIT+5"'),
                      _codeRow('curl -X POST localhost:8350/db/sql -d \'{"sql":"DELETE FROM telemetry WHERE id<100"}\''),
                    ],
                  ),
                ),
              ),
            ),
          ],
          if (_backupFiles.isNotEmpty) ...[
            const Divider(color: Colors.white12),
            _sectionHeader('Raw Backups (${_backupFiles.length} files)'),
            ..._backupFiles.map((f) => ListTile(
                  dense: true,
                  leading: const Icon(Icons.description, color: Colors.white38, size: 20),
                  title: Text(
                    f['name'] as String,
                    style: const TextStyle(color: Colors.white70, fontSize: 13, fontFamily: 'monospace'),
                  ),
                  subtitle: Text(
                    _formatSize(f['size'] as int),
                    style: const TextStyle(color: Colors.white38, fontSize: 11),
                  ),
                )),
          ],
        ],
      ),
    );
  }

  Widget _sectionHeader(String title) {
    return Padding(
      padding: const EdgeInsets.fromLTRB(16, 16, 16, 4),
      child: Text(
        title,
        style: const TextStyle(color: Colors.blue, fontSize: 13, fontWeight: FontWeight.bold),
      ),
    );
  }

  Widget _codeRow(String text) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 2),
      child: Text(
        text,
        style: const TextStyle(color: Colors.white54, fontSize: 11, fontFamily: 'monospace'),
      ),
    );
  }
}
