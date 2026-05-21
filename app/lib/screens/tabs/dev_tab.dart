import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../../services/ble_service.dart';
import '../../services/cloud_sync_service.dart';
import '../raw_log_screen.dart';
import '../ota_screen.dart';
import '../settings_screen.dart';
import '../history_screen.dart';
import '../scan_screen.dart';

class DevTab extends StatefulWidget {
  final BleService bleService;
  final CloudSyncService cloudSync;
  final bool connected;
  final bool skipBle;
  final int savedCount;
  final int rawCount;

  const DevTab({
    super.key,
    required this.bleService,
    required this.cloudSync,
    required this.connected,
    required this.skipBle,
    required this.savedCount,
    required this.rawCount,
  });

  @override
  State<DevTab> createState() => _DevTabState();
}

class _DevTabState extends State<DevTab> {
  bool get connected => widget.connected;
  bool get skipBle => widget.skipBle;
  int get savedCount => widget.savedCount;
  int get rawCount => widget.rawCount;
  BleService get bleService => widget.bleService;
  CloudSyncService get cloudSync => widget.cloudSync;
  bool _syncing = false;
  Map<String, dynamic>? _mgmt;
  bool _mgmtLoading = false;

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
        // Board management
        _boardMgmtCard(context),
        const SizedBox(height: 16),
        // Cloud sync
        _cloudSyncCard(context),
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

  Widget _boardMgmtCard(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: Colors.grey[850],
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: Colors.blueGrey.withValues(alpha: 0.3)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(Icons.developer_board, color: Colors.blueGrey, size: 20),
              const SizedBox(width: 8),
              Text('S3 Board',
                  style: TextStyle(color: Colors.grey[400], fontSize: 13)),
              const Spacer(),
              if (_mgmtLoading)
                const SizedBox(
                  width: 14, height: 14,
                  child: CircularProgressIndicator(strokeWidth: 2, color: Colors.blueGrey),
                )
              else
                IconButton(
                  icon: const Icon(Icons.refresh, size: 18),
                  color: Colors.blueGrey,
                  padding: EdgeInsets.zero,
                  constraints: const BoxConstraints(),
                  onPressed: connected ? _refreshMgmt : null,
                ),
            ],
          ),
          const SizedBox(height: 10),
          if (_mgmt != null) ...[
            _mgmtRow('Uptime', '${_mgmt!['uptime_h']}h ${_mgmt!['uptime_m']}m'),
            _mgmtRow('Heap', '${_mgmt!['heap_kb']} KB'),
            _mgmtRow('Board Temp', '${_mgmt!['board_temp']}°C'),
            _mgmtRow('Log Records', '${_mgmt!['log_records']}'),
            _mgmtRow('Log Used', '${_mgmt!['log_used_kb']} KB'),
            _mgmtRow('Log Free', '${_mgmt!['log_free_kb']} KB'),
            _mgmtRow('Trips', '${_mgmt!['trip_count']}'),
            _mgmtRow('ESP-NOW Peer', _mgmt!['peer_known'] ? 'Yes' : 'No'),
            const SizedBox(height: 12),
            Row(
              children: [
                Expanded(
                  child: OutlinedButton.icon(
                    onPressed: () async {
                      final ok = await bleService.clearLogs();
                      if (context.mounted) {
                        ScaffoldMessenger.of(context).showSnackBar(
                          SnackBar(content: Text(ok ? 'Logs cleared' : 'Failed')),
                        );
                        if (ok) _refreshMgmt();
                      }
                    },
                    icon: const Icon(Icons.delete_outline, size: 16),
                    label: const Text('Clear Logs'),
                    style: OutlinedButton.styleFrom(
                      foregroundColor: Colors.orange,
                      side: const BorderSide(color: Colors.orange),
                    ),
                  ),
                ),
                const SizedBox(width: 8),
                Expanded(
                  child: OutlinedButton.icon(
                    onPressed: () async {
                      final confirm = await showDialog<bool>(
                        context: context,
                        builder: (ctx) => AlertDialog(
                          backgroundColor: Colors.grey[850],
                          title: const Text('Restart S3?', style: TextStyle(color: Colors.white)),
                          actions: [
                            TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Cancel')),
                            TextButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Restart', style: TextStyle(color: Colors.red))),
                          ],
                        ),
                      );
                      if (confirm == true) await bleService.restartDevice();
                    },
                    icon: const Icon(Icons.restart_alt, size: 16),
                    label: const Text('Restart'),
                    style: OutlinedButton.styleFrom(
                      foregroundColor: Colors.redAccent,
                      side: const BorderSide(color: Colors.redAccent),
                    ),
                  ),
                ),
              ],
            ),
          ] else
            Center(
              child: TextButton.icon(
                onPressed: connected ? _refreshMgmt : null,
                icon: const Icon(Icons.download, size: 16),
                label: Text(connected ? 'Read Board Info' : 'Not connected'),
              ),
            ),
        ],
      ),
    );
  }

  Widget _mgmtRow(String label, String value) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 2),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        children: [
          Text(label, style: TextStyle(color: Colors.grey[500], fontSize: 12)),
          Text(value, style: const TextStyle(color: Colors.white, fontSize: 12, fontFamily: 'monospace', fontWeight: FontWeight.bold)),
        ],
      ),
    );
  }

  Future<void> _refreshMgmt() async {
    setState(() => _mgmtLoading = true);
    final info = await bleService.readMgmtInfo();
    if (mounted) setState(() { _mgmt = info; _mgmtLoading = false; });
  }

  Widget _cloudSyncCard(BuildContext context) {
    final running = cloudSync.isRunning;
    final error = cloudSync.lastError;
    final synced = cloudSync.lastSyncedCount;

    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: Colors.grey[850],
        borderRadius: BorderRadius.circular(12),
        border: Border.all(
          color: error != null
              ? Colors.redAccent.withValues(alpha: 0.3)
              : running
                  ? Colors.greenAccent.withValues(alpha: 0.3)
                  : Colors.grey[700]!,
        ),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(
                running ? Icons.cloud_done : Icons.cloud_off,
                color: running ? Colors.greenAccent : Colors.grey[600],
                size: 20,
              ),
              const SizedBox(width: 8),
              Text('Cloud Sync',
                  style: TextStyle(color: Colors.grey[400], fontSize: 13)),
              const Spacer(),
              if (_syncing)
                const SizedBox(
                  width: 14, height: 14,
                  child: CircularProgressIndicator(strokeWidth: 2, color: Colors.blue),
                )
              else
                Text(
                  running ? 'Auto (30s)' : 'Off',
                  style: TextStyle(
                    color: running ? Colors.greenAccent : Colors.grey[600],
                    fontSize: 11,
                  ),
                ),
            ],
          ),
          const SizedBox(height: 10),
          Row(
            children: [
              Text('Synced: $synced',
                  style: TextStyle(
                      color: Colors.blue, fontSize: 16,
                      fontFamily: 'monospace', fontWeight: FontWeight.bold)),
              const Spacer(),
              TextButton(
                onPressed: _syncing ? null : () async {
                  setState(() => _syncing = true);
                  final count = await cloudSync.syncOnce();
                  if (mounted) {
                    setState(() => _syncing = false);
                    ScaffoldMessenger.of(context).showSnackBar(
                      SnackBar(
                        content: Text(cloudSync.lastError != null
                            ? 'Sync error: ${cloudSync.lastError}'
                            : 'Synced $count rows'),
                        duration: const Duration(seconds: 2),
                      ),
                    );
                  }
                },
                child: const Text('Sync Now'),
              ),
              TextButton(
                onPressed: () => _showApiKeyDialog(context),
                child: const Text('API Key'),
              ),
            ],
          ),
          if (error != null)
            Padding(
              padding: const EdgeInsets.only(top: 6),
              child: Text(error,
                  style: const TextStyle(color: Colors.redAccent, fontSize: 11),
                  maxLines: 2, overflow: TextOverflow.ellipsis),
            ),
        ],
      ),
    );
  }

  Future<void> _showApiKeyDialog(BuildContext context) async {
    final prefs = await SharedPreferences.getInstance();
    final controller = TextEditingController(
        text: prefs.getString('cloud_api_key') ?? '');

    if (!context.mounted) return;
    final result = await showDialog<String>(
      context: context,
      builder: (ctx) => AlertDialog(
        backgroundColor: Colors.grey[850],
        title: const Text('Cloud API Key', style: TextStyle(color: Colors.white)),
        content: TextField(
          controller: controller,
          style: const TextStyle(color: Colors.white, fontFamily: 'monospace'),
          decoration: InputDecoration(
            hintText: 'Enter API key',
            hintStyle: TextStyle(color: Colors.grey[600]),
          ),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx),
            child: const Text('Cancel'),
          ),
          TextButton(
            onPressed: () => Navigator.pop(ctx, controller.text.trim()),
            child: const Text('Save'),
          ),
        ],
      ),
    );

    if (result != null) {
      await prefs.setString('cloud_api_key', result);
      if (result.isNotEmpty) {
        cloudSync.configure(apiKey: result);
        cloudSync.startPeriodic();
      } else {
        cloudSync.stop();
      }
      if (mounted) setState(() {});
    }
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
