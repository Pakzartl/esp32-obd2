import 'package:flutter/material.dart';
import '../services/ble_service.dart';
import '../services/ota_service.dart';

class OtaScreen extends StatefulWidget {
  final BleService bleService;

  const OtaScreen({super.key, required this.bleService});

  @override
  State<OtaScreen> createState() => _OtaScreenState();
}

class _OtaScreenState extends State<OtaScreen> {
  late final OtaService _otaService;
  String? _currentVersion;
  FirmwareInfo? _latestFirmware;
  bool _checking = true;
  bool _downloading = false;
  bool _updating = false;
  double _progress = 0;
  String? _error;
  String _status = 'Checking for updates...';

  @override
  void initState() {
    super.initState();
    _otaService = OtaService(widget.bleService);
    _checkUpdate();
  }

  Future<void> _checkUpdate() async {
    setState(() {
      _checking = true;
      _error = null;
    });

    _currentVersion = await widget.bleService.readFirmwareVersion();
    _latestFirmware = await _otaService.checkForUpdate();

    setState(() {
      _checking = false;
      if (_latestFirmware == null) {
        _status = 'Could not check for updates';
      } else if (_currentVersion != null &&
          !_otaService.isNewer(_latestFirmware!.version, _currentVersion!)) {
        _status = 'Firmware is up to date';
      } else {
        _status = 'Update available';
      }
    });
  }

  Future<void> _startUpdate() async {
    if (_latestFirmware?.downloadUrl == null) return;

    setState(() {
      _downloading = true;
      _status = 'Downloading firmware...';
      _error = null;
    });

    final firmware = await _otaService.downloadFirmware(_latestFirmware!.downloadUrl!);
    if (firmware == null) {
      setState(() {
        _downloading = false;
        _error = 'Download failed';
        _status = 'Update available';
      });
      return;
    }

    setState(() {
      _downloading = false;
      _updating = true;
      _progress = 0;
      _status = 'Uploading to device...';
    });

    final ok = await _otaService.performOta(firmware, (p) {
      setState(() => _progress = p);
    });

    if (ok) {
      setState(() {
        _updating = false;
        _status = 'Update complete! Device is rebooting...';
      });
    } else {
      setState(() {
        _updating = false;
        _error = 'Update failed';
        _status = 'Update available';
      });
    }
  }

  bool get _hasUpdate =>
      _latestFirmware != null &&
      _currentVersion != null &&
      _otaService.isNewer(_latestFirmware!.version, _currentVersion!);

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Firmware Update')),
      body: Padding(
        padding: const EdgeInsets.all(24),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Card(
              child: Padding(
                padding: const EdgeInsets.all(20),
                child: Column(
                  children: [
                    Icon(
                      _hasUpdate ? Icons.system_update : Icons.check_circle,
                      size: 64,
                      color: _hasUpdate ? Colors.orange : Colors.green,
                    ),
                    const SizedBox(height: 16),
                    Text(_status,
                        style: Theme.of(context).textTheme.titleLarge),
                    const SizedBox(height: 8),
                    Text(
                      'Current: ${_currentVersion ?? "unknown"}',
                      style: Theme.of(context).textTheme.bodyMedium,
                    ),
                    if (_latestFirmware != null)
                      Text(
                        'Latest: ${_latestFirmware!.version}',
                        style: Theme.of(context).textTheme.bodyMedium,
                      ),
                  ],
                ),
              ),
            ),
            if (_error != null) ...[
              const SizedBox(height: 12),
              Text(_error!,
                  style: const TextStyle(color: Colors.red),
                  textAlign: TextAlign.center),
            ],
            if (_updating) ...[
              const SizedBox(height: 24),
              LinearProgressIndicator(value: _progress),
              const SizedBox(height: 8),
              Text('${(_progress * 100).toStringAsFixed(1)}%',
                  textAlign: TextAlign.center),
            ],
            if (_latestFirmware?.changelog.isNotEmpty == true && _hasUpdate) ...[
              const SizedBox(height: 24),
              Text('Changelog', style: Theme.of(context).textTheme.titleMedium),
              const SizedBox(height: 8),
              Expanded(
                child: SingleChildScrollView(
                  child: Text(_latestFirmware!.changelog),
                ),
              ),
            ] else
              const Spacer(),
            const SizedBox(height: 16),
            if (_hasUpdate && !_updating && !_downloading)
              FilledButton.icon(
                onPressed: _startUpdate,
                icon: const Icon(Icons.download),
                label: Text(
                    'Update to ${_latestFirmware!.version} (${(_latestFirmware!.size / 1024).toStringAsFixed(0)} KB)'),
              ),
            if (_checking || _downloading)
              const Center(child: CircularProgressIndicator()),
            if (!_checking && !_hasUpdate && _error == null && !_updating)
              OutlinedButton.icon(
                onPressed: _checkUpdate,
                icon: const Icon(Icons.refresh),
                label: const Text('Check again'),
              ),
          ],
        ),
      ),
    );
  }
}
