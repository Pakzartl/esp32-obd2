import 'dart:convert';
import 'dart:typed_data';
import 'package:http/http.dart' as http;
import 'ble_service.dart';

class FirmwareInfo {
  final String version;
  final String changelog;
  final String? downloadUrl;
  final int size;

  FirmwareInfo({
    required this.version,
    required this.changelog,
    this.downloadUrl,
    required this.size,
  });

  factory FirmwareInfo.fromJson(Map<String, dynamic> json) {
    return FirmwareInfo(
      version: json['version'] ?? '0.0.0',
      changelog: json['changelog'] ?? '',
      downloadUrl: json['download_url'],
      size: json['size'] ?? 0,
    );
  }
}

class OtaService {
  static const String _apiBase = 'https://adv350.pakzartl.xyz';
  static const int _chunkSize = 240;

  final BleService _ble;

  OtaService(this._ble);

  Future<FirmwareInfo?> checkForUpdate() async {
    try {
      final res = await http.get(Uri.parse('$_apiBase/api/firmware/latest'));
      if (res.statusCode != 200) return null;
      return FirmwareInfo.fromJson(jsonDecode(res.body));
    } catch (_) {
      return null;
    }
  }

  bool isNewer(String remote, String current) {
    final r = _parseVersion(remote);
    final c = _parseVersion(current);
    for (int i = 0; i < 3; i++) {
      final rv = i < r.length ? r[i] : 0;
      final cv = i < c.length ? c[i] : 0;
      if (rv > cv) return true;
      if (rv < cv) return false;
    }
    return remote != current;
  }

  static List<int> _parseVersion(String v) {
    final clean = v.split('-').first;
    return clean.split('.').map((s) => int.tryParse(s) ?? 0).toList();
  }

  Future<Uint8List?> downloadFirmware(String url) async {
    try {
      final res = await http.get(Uri.parse(url));
      if (res.statusCode != 200) return null;
      return res.bodyBytes;
    } catch (_) {
      return null;
    }
  }

  Future<bool> performOta(
    Uint8List firmware,
    void Function(double progress) onProgress,
  ) async {
    await _ble.abortOta();
    await Future.delayed(const Duration(milliseconds: 300));
    final ok = await _ble.startOta(firmware.length);
    if (!ok) return false;

    await Future.delayed(const Duration(milliseconds: 200));

    int sent = 0;
    while (sent < firmware.length) {
      final end =
          (sent + _chunkSize > firmware.length) ? firmware.length : sent + _chunkSize;
      final chunk = firmware.sublist(sent, end);

      final wrote = await _ble.writeOtaChunk(chunk);
      if (!wrote) {
        await _ble.abortOta();
        return false;
      }

      sent = end;
      onProgress(sent / firmware.length);

      if (sent % (_chunkSize * 20) == 0) {
        await Future.delayed(const Duration(milliseconds: 50));
      }
    }

    await Future.delayed(const Duration(milliseconds: 500));
    return await _ble.finishOta();
  }
}
