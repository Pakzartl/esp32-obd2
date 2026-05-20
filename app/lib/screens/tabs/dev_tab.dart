import 'package:flutter/material.dart';
import '../../services/ble_service.dart';
import '../raw_log_screen.dart';
import '../ota_screen.dart';
import '../settings_screen.dart';
import '../history_screen.dart';
import '../scan_screen.dart';

class DevTab extends StatelessWidget {
  final BleService bleService;
  final bool connected;
  final bool skipBle;
  final int savedCount;
  final int rawCount;

  const DevTab({
    super.key,
    required this.bleService,
    required this.connected,
    required this.skipBle,
    required this.savedCount,
    required this.rawCount,
  });

  @override
  Widget build(BuildContext context) {
    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        // BLE status card
        _statusCard(),
        const SizedBox(height: 16),
        // Debug counters
        _countersCard(),
        const SizedBox(height: 16),
        // Raw BLE log
        _actionTile(
          context,
          icon: Icons.terminal,
          label: 'Raw BLE Log',
          subtitle: 'Live CAN frame viewer',
          color: Colors.lightGreenAccent,
          onTap: () => Navigator.push(
            context,
            MaterialPageRoute(
              builder: (_) => Scaffold(
                appBar: AppBar(
                  title: const Text('Raw CAN Log'),
                  backgroundColor: Colors.black87,
                  foregroundColor: Colors.white,
                ),
                backgroundColor: Colors.grey[900],
                body: RawLogScreen(bleService: bleService),
              ),
            ),
          ),
        ),
        const SizedBox(height: 8),
        // OTA
        _actionTile(
          context,
          icon: Icons.system_update,
          label: 'Firmware Update',
          subtitle: 'OTA update via BLE',
          color: Colors.orange,
          onTap: () => Navigator.push(
            context,
            MaterialPageRoute(
              builder: (_) => OtaScreen(bleService: bleService),
            ),
          ),
        ),
        const SizedBox(height: 8),
        // History
        _actionTile(
          context,
          icon: Icons.history,
          label: 'Trip History',
          subtitle: 'Past recorded sessions',
          color: Colors.blue,
          onTap: () => Navigator.push(
            context,
            MaterialPageRoute(builder: (_) => const HistoryScreen()),
          ),
        ),
        const SizedBox(height: 8),
        // Settings
        _actionTile(
          context,
          icon: Icons.settings,
          label: 'Settings',
          subtitle: 'Dev mode, backups, board scanner',
          color: Colors.grey,
          onTap: () => Navigator.push(
            context,
            MaterialPageRoute(builder: (_) => const SettingsScreen()),
          ),
        ),
        const SizedBox(height: 8),
        // Ping
        _actionTile(
          context,
          icon: Icons.sensors,
          label: 'Ping Device',
          subtitle: 'LED blink test',
          color: Colors.cyan,
          onTap: () async {
            final ok = await bleService.ping();
            if (context.mounted) {
              ScaffoldMessenger.of(context).showSnackBar(
                SnackBar(
                  content: Text(ok ? 'Ping OK' : 'Ping failed'),
                  duration: const Duration(seconds: 1),
                ),
              );
            }
          },
        ),
        const SizedBox(height: 24),
        // Disconnect button
        SizedBox(
          width: double.infinity,
          child: OutlinedButton.icon(
            onPressed: () async {
              await bleService.clearLastDevice();
              await bleService.disconnect();
              if (context.mounted) {
                Navigator.pushReplacement(
                  context,
                  MaterialPageRoute(builder: (_) => const ScanScreen()),
                );
              }
            },
            icon: const Icon(Icons.bluetooth_disabled, size: 18),
            label: const Text('Disconnect & Scan'),
            style: OutlinedButton.styleFrom(
              foregroundColor: Colors.redAccent,
              side: const BorderSide(color: Colors.redAccent),
              padding: const EdgeInsets.symmetric(vertical: 14),
              shape: RoundedRectangleBorder(
                borderRadius: BorderRadius.circular(12),
              ),
            ),
          ),
        ),
      ],
    );
  }

  Widget _statusCard() {
    final Color statusColor;
    final String statusText;
    final IconData statusIcon;

    if (skipBle) {
      statusColor = Colors.orange;
      statusText = 'BLE Skipped (Demo Mode)';
      statusIcon = Icons.bug_report;
    } else if (connected) {
      statusColor = Colors.greenAccent;
      statusText = 'Connected';
      statusIcon = Icons.bluetooth_connected;
    } else {
      statusColor = Colors.redAccent;
      statusText = 'Disconnected';
      statusIcon = Icons.bluetooth_disabled;
    }

    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: statusColor.withValues(alpha: 0.1),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: statusColor.withValues(alpha: 0.3)),
      ),
      child: Row(
        children: [
          Icon(statusIcon, color: statusColor, size: 24),
          const SizedBox(width: 12),
          Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                'BLE Status',
                style: TextStyle(color: Colors.grey[500], fontSize: 11),
              ),
              Text(
                statusText,
                style: TextStyle(
                  color: statusColor,
                  fontSize: 16,
                  fontWeight: FontWeight.bold,
                ),
              ),
            ],
          ),
        ],
      ),
    );
  }

  Widget _countersCard() {
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: Colors.grey[850],
        borderRadius: BorderRadius.circular(12),
      ),
      child: Row(
        children: [
          Expanded(
            child: _counterItem(
              'DB Saved',
              '$savedCount',
              Icons.storage,
              Colors.blue,
            ),
          ),
          Container(
            width: 1,
            height: 40,
            color: Colors.grey[700],
          ),
          Expanded(
            child: _counterItem(
              'Raw Logged',
              '$rawCount',
              Icons.fiber_manual_record,
              Colors.redAccent,
            ),
          ),
        ],
      ),
    );
  }

  Widget _counterItem(
      String label, String value, IconData icon, Color color) {
    return Column(
      children: [
        Icon(icon, color: color, size: 16),
        const SizedBox(height: 4),
        Text(
          value,
          style: TextStyle(
            color: color,
            fontSize: 22,
            fontWeight: FontWeight.bold,
            fontFamily: 'monospace',
          ),
        ),
        Text(label, style: TextStyle(color: Colors.grey[500], fontSize: 11)),
      ],
    );
  }

  Widget _actionTile(
    BuildContext context, {
    required IconData icon,
    required String label,
    required String subtitle,
    required Color color,
    required VoidCallback onTap,
  }) {
    return Material(
      color: Colors.grey[850],
      borderRadius: BorderRadius.circular(12),
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(12),
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 14),
          child: Row(
            children: [
              Icon(icon, color: color, size: 22),
              const SizedBox(width: 14),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(label,
                        style: const TextStyle(
                            color: Colors.white, fontSize: 15)),
                    Text(subtitle,
                        style: TextStyle(
                            color: Colors.grey[600], fontSize: 12)),
                  ],
                ),
              ),
              Icon(Icons.chevron_right, color: Colors.grey[700], size: 20),
            ],
          ),
        ),
      ),
    );
  }
}
