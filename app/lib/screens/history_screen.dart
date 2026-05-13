import 'package:flutter/material.dart';
import 'package:intl/intl.dart';
import '../models/telemetry.dart';
import '../services/database_service.dart';

class HistoryScreen extends StatefulWidget {
  const HistoryScreen({super.key});

  @override
  State<HistoryScreen> createState() => _HistoryScreenState();
}

class _HistoryScreenState extends State<HistoryScreen> {
  final _db = DatabaseService();
  List<Telemetry> _records = [];
  int _unsyncedCount = 0;
  bool _loading = true;

  @override
  void initState() {
    super.initState();
    _loadData();
  }

  Future<void> _loadData() async {
    final records = await _db.getRecent(limit: 200);
    final unsynced = await _db.getUnsyncedCount();
    setState(() {
      _records = records;
      _unsyncedCount = unsynced;
      _loading = false;
    });
  }

  @override
  Widget build(BuildContext context) {
    final fmt = DateFormat('HH:mm:ss');
    return Scaffold(
      appBar: AppBar(
        title: const Text('Trip History'),
        backgroundColor: Colors.black87,
        foregroundColor: Colors.white,
        actions: [
          Padding(
            padding: const EdgeInsets.only(right: 16),
            child: Center(
              child: Text(
                '$_unsyncedCount pending sync',
                style: TextStyle(
                  color: _unsyncedCount > 0 ? Colors.orange : Colors.green,
                  fontSize: 13,
                ),
              ),
            ),
          ),
        ],
      ),
      backgroundColor: Colors.grey[100],
      body: _loading
          ? const Center(child: CircularProgressIndicator())
          : _records.isEmpty
              ? const Center(child: Text('No records yet'))
              : ListView.builder(
                  itemCount: _records.length,
                  itemBuilder: (context, index) {
                    final t = _records[index];
                    return ListTile(
                      dense: true,
                      leading: Icon(
                        t.synced ? Icons.cloud_done : Icons.cloud_off,
                        color: t.synced ? Colors.green : Colors.orange,
                        size: 20,
                      ),
                      title: Text(
                        '${t.speed} km/h  |  ${t.rpm} rpm  |  Gear ${t.gear == 0 ? "N" : t.gear}',
                        style: const TextStyle(fontSize: 14),
                      ),
                      subtitle: Text(
                        '${fmt.format(t.timestamp)}  |  ${t.coolantTemp}°C  |  Fuel ${t.fuelLevel}%',
                        style: TextStyle(fontSize: 12, color: Colors.grey[600]),
                      ),
                    );
                  },
                ),
    );
  }
}
