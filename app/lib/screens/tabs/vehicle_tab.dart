import 'package:flutter/material.dart';
import '../../models/telemetry.dart';

class VehicleTab extends StatelessWidget {
  final Telemetry current;

  const VehicleTab({super.key, required this.current});

  @override
  Widget build(BuildContext context) {
    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        _sectionHeader('Engine'),
        _buildGrid([
          _SensorData('RPM', '${current.rpm}', '',
              current.rpm > 7000 ? Colors.red : Colors.orange, Icons.speed),
          _SensorData('Speed', '${current.speed}', 'km/h', Colors.blue,
              Icons.speed),
          _SensorData(
              'Throttle',
              '${current.throttle}',
              '%',
              Colors.green,
              Icons.flash_on),
          _SensorData('MAP', '${current.mapKpa}', 'kPa', Colors.purple,
              Icons.compress),
        ]),
        const SizedBox(height: 20),
        _sectionHeader('Temperature'),
        _buildGrid([
          _SensorData(
              'Coolant',
              '${current.coolantTemp}',
              '°C',
              current.coolantTemp > 105
                  ? Colors.red
                  : current.coolantTemp > 90
                      ? Colors.orange
                      : Colors.cyan,
              Icons.thermostat),
          _SensorData(
              'IAT', '${current.iat}', '°C', Colors.teal, Icons.air),
        ]),
        const SizedBox(height: 20),
        _sectionHeader('Fuel & Electrical'),
        _buildGrid([
          _SensorData('Fuel Rate', current.fuelRateLph.toStringAsFixed(1),
              'L/h', Colors.orange, Icons.local_gas_station),
          _SensorData(
              'km/L (est.)',
              current.speed > 0 && current.fuelRateLph > 0.01
                  ? (current.speed / current.fuelRateLph).toStringAsFixed(1)
                  : '--',
              '',
              Colors.green,
              Icons.eco),
          _SensorData('CVT Ratio', current.cvtRatio.toStringAsFixed(2), '',
              Colors.indigo, Icons.settings),
        ]),
        const SizedBox(height: 20),
        _sectionHeader('Ride Quality'),
        _buildGrid([
          _SensorData(
              'Ride Score',
              '${current.ridingScore}',
              '/100',
              current.ridingScore >= 80
                  ? Colors.greenAccent
                  : current.ridingScore >= 50
                      ? Colors.orange
                      : Colors.redAccent,
              Icons.star),
        ]),
        const SizedBox(height: 20),
        _sectionHeader('Raw Data'),
        Container(
          padding: const EdgeInsets.all(12),
          decoration: BoxDecoration(
            color: Colors.grey[850],
            borderRadius: BorderRadius.circular(12),
          ),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              _rawRow('Raw BLE Hex',
                  current.rawBleHex.isEmpty ? '--' : current.rawBleHex),
              const SizedBox(height: 8),
              _rawRow('Timestamp', _formatTimestamp(current.timestamp)),
            ],
          ),
        ),
        const SizedBox(height: 20),
        _sectionHeader('DTC Status'),
        Container(
          padding: const EdgeInsets.all(16),
          decoration: BoxDecoration(
            color: Colors.grey[850],
            borderRadius: BorderRadius.circular(12),
            border: Border.all(color: Colors.grey[800]!),
          ),
          child: Row(
            children: [
              Icon(Icons.check_circle, color: Colors.green[400], size: 20),
              const SizedBox(width: 10),
              Text(
                'No DTCs detected',
                style: TextStyle(color: Colors.grey[400], fontSize: 14),
              ),
            ],
          ),
        ),
      ],
    );
  }

  Widget _sectionHeader(String title) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 10),
      child: Text(
        title,
        style: const TextStyle(
          color: Colors.blue,
          fontSize: 13,
          fontWeight: FontWeight.bold,
          letterSpacing: 0.5,
        ),
      ),
    );
  }

  Widget _buildGrid(List<_SensorData> sensors) {
    return GridView.count(
      crossAxisCount: 2,
      shrinkWrap: true,
      physics: const NeverScrollableScrollPhysics(),
      mainAxisSpacing: 10,
      crossAxisSpacing: 10,
      childAspectRatio: 1.6,
      children: sensors.map(_buildSensorTile).toList(),
    );
  }

  Widget _buildSensorTile(_SensorData sensor) {
    return Container(
      decoration: BoxDecoration(
        color: Colors.grey[850],
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: sensor.color.withValues(alpha: 0.2)),
      ),
      padding: const EdgeInsets.all(12),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Row(
            children: [
              Icon(sensor.icon, color: sensor.color.withValues(alpha: 0.6),
                  size: 14),
              const SizedBox(width: 4),
              Expanded(
                child: Text(
                  sensor.label,
                  style: TextStyle(color: Colors.grey[500], fontSize: 11),
                  overflow: TextOverflow.ellipsis,
                ),
              ),
            ],
          ),
          const Spacer(),
          FittedBox(
            fit: BoxFit.scaleDown,
            alignment: Alignment.centerLeft,
            child: Row(
              crossAxisAlignment: CrossAxisAlignment.baseline,
              textBaseline: TextBaseline.alphabetic,
              children: [
                Text(
                  sensor.value,
                  style: TextStyle(
                    color: sensor.color,
                    fontSize: 28,
                    fontWeight: FontWeight.bold,
                    fontFamily: 'monospace',
                  ),
                ),
                if (sensor.unit.isNotEmpty) ...[
                  const SizedBox(width: 4),
                  Text(
                    sensor.unit,
                    style: TextStyle(color: Colors.grey[600], fontSize: 12),
                  ),
                ],
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _rawRow(String label, String value) {
    return Row(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        SizedBox(
          width: 100,
          child: Text(label,
              style: TextStyle(color: Colors.grey[500], fontSize: 12)),
        ),
        Expanded(
          child: Text(
            value,
            style: const TextStyle(
              color: Colors.lightGreenAccent,
              fontSize: 12,
              fontFamily: 'monospace',
            ),
          ),
        ),
      ],
    );
  }

  String _formatTimestamp(DateTime dt) {
    return '${dt.hour.toString().padLeft(2, '0')}:'
        '${dt.minute.toString().padLeft(2, '0')}:'
        '${dt.second.toString().padLeft(2, '0')}.'
        '${dt.millisecond.toString().padLeft(3, '0')}';
  }
}

class _SensorData {
  final String label;
  final String value;
  final String unit;
  final Color color;
  final IconData icon;

  _SensorData(this.label, this.value, this.unit, this.color, this.icon);
}
