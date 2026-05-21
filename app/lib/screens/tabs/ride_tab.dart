import 'package:flutter/material.dart';
import '../../models/telemetry.dart';

enum BleState { connected, disconnected, connecting }

class RideTab extends StatelessWidget {
  final Telemetry current;
  final BleState bleState;

  const RideTab({
    super.key,
    required this.current,
    required this.bleState,
  });

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        // Connection dot
        Padding(
          padding: const EdgeInsets.only(top: 12, right: 16),
          child: Row(
            mainAxisAlignment: MainAxisAlignment.end,
            children: [
              Container(
                width: 8,
                height: 8,
                decoration: BoxDecoration(
                  shape: BoxShape.circle,
                  color: switch (bleState) {
                    BleState.connected => Colors.greenAccent,
                    BleState.connecting => Colors.orange,
                    BleState.disconnected => Colors.redAccent,
                  },
                ),
              ),
              const SizedBox(width: 6),
              Text(
                switch (bleState) {
                  BleState.connected => 'Live',
                  BleState.connecting => 'Connecting',
                  BleState.disconnected => 'Disconnected',
                },
                style: TextStyle(
                  color: switch (bleState) {
                    BleState.connected => Colors.greenAccent,
                    BleState.connecting => Colors.orange,
                    BleState.disconnected => Colors.redAccent,
                  },
                  fontSize: 11,
                ),
              ),
            ],
          ),
        ),
        // Hero speed
        Expanded(
          flex: 3,
          child: Center(
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                Text(
                  '${current.speed}',
                  style: const TextStyle(
                    color: Colors.white,
                    fontSize: 96,
                    fontWeight: FontWeight.w700,
                    fontFamily: 'monospace',
                    height: 1.0,
                  ),
                ),
                Text(
                  'km/h',
                  style: TextStyle(
                    color: Colors.grey[600],
                    fontSize: 16,
                    fontWeight: FontWeight.w300,
                  ),
                ),
              ],
            ),
          ),
        ),
        // Fuel efficiency suggestion
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 24),
          child: _FuelHint(speed: current.speed, fuelRate: current.fuelRateLph, rpm: current.rpm),
        ),
        const SizedBox(height: 12),
        // RPM bar
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 24),
          child: _RpmBar(rpm: current.rpm),
        ),
        const SizedBox(height: 20),
        // Secondary gauges
        Expanded(
          flex: 3,
          child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: 24),
            child: Column(
              children: [
                Expanded(
                  child: Row(
                    children: [
                      Expanded(
                        child: _SecondaryGauge(
                          label: 'Coolant',
                          value: '${current.coolantTemp}',
                          unit: '°C',
                          color: current.coolantTemp > 105
                              ? Colors.red
                              : current.coolantTemp > 90
                                  ? Colors.orange
                                  : Colors.cyan,
                          icon: Icons.thermostat,
                        ),
                      ),
                      const SizedBox(width: 12),
                      Expanded(
                        child: _SecondaryGauge(
                          label: 'Throttle',
                          value: '${current.throttle}',
                          unit: '%',
                          color: Colors.greenAccent,
                          icon: Icons.flash_on,
                        ),
                      ),
                    ],
                  ),
                ),
                const SizedBox(height: 10),
                Expanded(
                  child: Row(
                    children: [
                      Expanded(
                        child: _SecondaryGauge(
                          label: 'Fuel',
                          value: current.fuelRateLph.toStringAsFixed(1),
                          unit: 'L/h',
                          color: Colors.orange,
                          icon: Icons.local_gas_station,
                        ),
                      ),
                      const SizedBox(width: 12),
                      Expanded(
                        child: _SecondaryGauge(
                          label: 'MAP',
                          value: '${current.mapKpa}',
                          unit: 'kPa',
                          color: Colors.purple,
                          icon: Icons.compress,
                        ),
                      ),
                    ],
                  ),
                ),
              ],
            ),
          ),
        ),
        const SizedBox(height: 16),
      ],
    );
  }
}

class _RpmBar extends StatelessWidget {
  final int rpm;
  static const int maxRpm = 9000;

  const _RpmBar({required this.rpm});

  @override
  Widget build(BuildContext context) {
    final fraction = (rpm / maxRpm).clamp(0.0, 1.0);
    final color = rpm > 7000
        ? Colors.red
        : rpm > 5000
            ? Colors.orange
            : Colors.blue;

    return Column(
      children: [
        Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            Text(
              'RPM',
              style: TextStyle(color: Colors.grey[600], fontSize: 11),
            ),
            Text(
              '$rpm',
              style: TextStyle(
                color: color,
                fontSize: 18,
                fontWeight: FontWeight.bold,
                fontFamily: 'monospace',
              ),
            ),
          ],
        ),
        const SizedBox(height: 6),
        ClipRRect(
          borderRadius: BorderRadius.circular(4),
          child: LinearProgressIndicator(
            value: fraction,
            minHeight: 10,
            backgroundColor: Colors.grey[800],
            color: color,
          ),
        ),
        const SizedBox(height: 4),
        Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            Text('0', style: TextStyle(color: Colors.grey[700], fontSize: 9)),
            Text('3k', style: TextStyle(color: Colors.grey[700], fontSize: 9)),
            Text('6k', style: TextStyle(color: Colors.grey[700], fontSize: 9)),
            Text('9k', style: TextStyle(color: Colors.grey[700], fontSize: 9)),
          ],
        ),
      ],
    );
  }
}

class _SecondaryGauge extends StatelessWidget {
  final String label;
  final String value;
  final String unit;
  final Color color;
  final IconData icon;

  const _SecondaryGauge({
    required this.label,
    required this.value,
    required this.unit,
    required this.color,
    required this.icon,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      decoration: BoxDecoration(
        color: Colors.grey[850],
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: color.withValues(alpha: 0.2)),
      ),
      padding: const EdgeInsets.symmetric(vertical: 16, horizontal: 12),
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(icon, color: color.withValues(alpha: 0.6), size: 14),
              const SizedBox(width: 4),
              Text(
                label,
                style: TextStyle(color: Colors.grey[500], fontSize: 12),
              ),
            ],
          ),
          const Spacer(),
          FittedBox(
            fit: BoxFit.scaleDown,
            child: Text(
              value,
              style: TextStyle(
                color: color,
                fontSize: 36,
                fontWeight: FontWeight.bold,
                fontFamily: 'monospace',
              ),
            ),
          ),
          Text(
            unit,
            style: TextStyle(color: Colors.grey[600], fontSize: 12),
          ),
          const Spacer(),
        ],
      ),
    );
  }
}

class _FuelHint extends StatelessWidget {
  final int speed;
  final double fuelRate;
  final int rpm;

  const _FuelHint({required this.speed, required this.fuelRate, required this.rpm});

  @override
  Widget build(BuildContext context) {
    if (rpm == 0) return const SizedBox.shrink();

    final Color color;
    final String hint;
    final IconData icon;

    if (speed == 0) {
      // Idling
      color = Colors.orange;
      hint = 'Idle ${fuelRate.toStringAsFixed(1)} L/h';
      icon = Icons.pause_circle_outline;
    } else if (fuelRate < 0.01) {
      color = Colors.grey;
      hint = '--';
      icon = Icons.eco;
    } else {
      final kmpl = speed / fuelRate;
      if (kmpl >= 30) {
        color = Colors.greenAccent;
        hint = '${kmpl.round()} km/L — Eco';
        icon = Icons.eco;
      } else if (kmpl >= 15) {
        color = Colors.orange;
        hint = '${kmpl.round()} km/L — Moderate';
        icon = Icons.local_gas_station;
      } else {
        color = Colors.redAccent;
        hint = '${kmpl.round()} km/L — Heavy';
        icon = Icons.local_fire_department;
      }
    }

    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.1),
        borderRadius: BorderRadius.circular(20),
        border: Border.all(color: color.withValues(alpha: 0.3)),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Icon(icon, color: color, size: 16),
          const SizedBox(width: 8),
          Text(
            hint,
            style: TextStyle(
              color: color,
              fontSize: 14,
              fontWeight: FontWeight.w600,
            ),
          ),
        ],
      ),
    );
  }
}
