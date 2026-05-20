import 'dart:io';
import 'package:path_provider/path_provider.dart';

class RawBackupService {
  IOSink? _sink;
  String? _currentPath;
  int _count = 0;

  String? get currentPath => _currentPath;
  int get count => _count;

  Future<void> startSession() async {
    final dir = await getApplicationDocumentsDirectory();
    final backupDir = Directory('${dir.path}/raw_backup');
    if (!await backupDir.exists()) {
      await backupDir.create(recursive: true);
    }
    final now = DateTime.now();
    final filename = '${now.year}${_p(now.month)}${_p(now.day)}_${_p(now.hour)}${_p(now.minute)}${_p(now.second)}.jsonl';
    _currentPath = '${backupDir.path}/$filename';
    _sink = File(_currentPath!).openWrite(mode: FileMode.append);
    _count = 0;
  }

  void writeRaw(List<int> bleBytes) {
    if (_sink == null) return;
    final ts = DateTime.now().millisecondsSinceEpoch;
    final hex = bleBytes.map((b) => b.toRadixString(16).padLeft(2, '0')).join('');
    _sink!.writeln('{"ts":$ts,"hex":"$hex","len":${bleBytes.length}}');
    _count++;
    // Flush every 50 packets — survive app crash/kill
    if (_count % 50 == 0) {
      _sink!.flush();
    }
  }

  Future<void> endSession() async {
    await _sink?.flush();
    await _sink?.close();
    _sink = null;
  }

  Future<List<FileSystemEntity>> listBackups() async {
    final dir = await getApplicationDocumentsDirectory();
    final backupDir = Directory('${dir.path}/raw_backup');
    if (!await backupDir.exists()) return [];
    final files = await backupDir.list().toList();
    files.sort((a, b) => b.path.compareTo(a.path));
    return files;
  }

  String _p(int n) => n.toString().padLeft(2, '0');
}
