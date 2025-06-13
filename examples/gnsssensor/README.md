# GNSS Daemon

## Overview

The GNSS daemon is a continuous monitoring service for Global Navigation Satellite System (GNSS) data. It provides functionality to start and stop GNSS monitoring and retrieve positioning information.

## Features

- Start/stop continuous GNSS monitoring
- Single-shot GNSS data retrieval
- JSON-formatted output for easy integration with other systems
- Configurable polling interval
- Support for multiple satellite systems (GPS, GLONASS, etc.)
- Detailed satellite and positioning accuracy information

## Usage

```
Usage: gnsssensor [command] [options]
Commands:
  start     Start continuous GNSS monitoring
  stop      Stop GNSS monitoring
  (none)    Read GNSS data once
Options:
  -i ID     Specify request ID (for daemon mode)
  -t MS     Specify polling interval in milliseconds (default: 1000)
  -s SYS    Specify satellite system (default: 3 - GPS+GLONASS)
  -h        Show this help message
```

## Examples

### Start GNSS monitoring with 2-second interval
```
gnsssensor start -t 2000
```

### Stop GNSS monitoring
```
gnsssensor stop
```

### Get a single GNSS reading
```
gnsssensor
```

## Output Format

The daemon outputs data in JSON format. Here's an example of the output when a position fix is obtained:

```json
{
  "data": {
    "time": "12:34:56.123456",
    "lat": "35.12.3456",
    "lng": "139.45.6789",
    "lat_raw": 35.12345678,
    "lng_raw": 139.45678901,
    "fix": 3,
    "altitude": 123.45,
    "geoid": 36.78,
    "visible_sats": 12,
    "tracking_sats": 10,
    "pos_sats": 8,
    "h_accuracy": 1.25,
    "v_accuracy": 2.45,
    "pdop": 1.8,
    "hdop": 1.2,
    "vdop": 1.5,
    "tdop": 0.9
  },
  "status": {
    "code": 0,
    "msg": "Position fixed"
  }
}
```

When no position fix is available:

```json
{
  "data": {
    "time": "12:34:56.123456",
    "fix": 0,
    "visible_sats": 3,
    "tracking_sats": 2
  },
  "status": {
    "code": 0,
    "msg": "No position fix"
  }
}
```

For daemon mode responses:

```json
{
  "cmd": "gnss",
  "type": "poll",
  "interval": 1000,
  "id": 1,
  "data": {
    "time": "12:34:56.123456",
    "lat": "35.12.3456",
    "lng": "139.45.6789",
    "fix": 3,
    "altitude": 123.45,
    "geoid": 36.78,
    "visible_sats": 12,
    "tracking_sats": 10,
    "pos_sats": 8,
    "h_accuracy": 1.25,
    "v_accuracy": 2.45,
    "pdop": 1.8,
    "hdop": 1.2,
    "vdop": 1.5,
    "tdop": 0.9
  },
  "status": {
    "code": 0,
    "msg": "Position fixed"
  }
}
```

Error responses:

```json
{  "status": {
    "code": -1,
    "msg": "Error message"
  }
}
```

## Data Fields Explanation

### Basic Position Data
- `time`: Current UTC time in HH:MM:SS.ssssss format
- `lat`: Latitude in degrees.minutes.decimal format
- `lng`: Longitude in degrees.minutes.decimal format
- `lat_raw`: Latitude in decimal degrees format
- `lng_raw`: Longitude in decimal degrees format
- `fix`: Position fix mode (0: Invalid, 2: 2D fix, 3: 3D fix)
- `altitude`: Height above sea level in meters

### Additional Data
- `geoid`: Geoid height (difference between WGS-84 ellipsoid and mean sea level) in meters
- `visible_sats`: Number of visible satellites
- `tracking_sats`: Number of satellites being tracked
- `pos_sats`: Number of satellites used for position calculation
- `h_accuracy`: Horizontal position accuracy in meters
- `v_accuracy`: Vertical position accuracy in meters

### Dilution of Precision (DOP) Values
- `pdop`: Position Dilution of Precision - overall position accuracy
- `hdop`: Horizontal Dilution of Precision - horizontal position accuracy
- `vdop`: Vertical Dilution of Precision - vertical position accuracy
- `tdop`: Time Dilution of Precision - time accuracy

Lower DOP values indicate better positioning precision.

## Satellite Systems

The `-s` option allows you to specify which satellite systems to use. The value is a bit field where each bit represents a different satellite system:

| Bit | Value | Satellite System | Description |
|-----|-------|-----------------|-------------|
| 0   | 1     | GPS             | Global Positioning System (USA) |
| 1   | 2     | GLONASS         | GLObal NAvigation Satellite System (Russia) |
| 2   | 4     | SBAS            | Satellite Based Augmentation System |
| 3   | 8     | QZSS L1CA       | Quasi-Zenith Satellite System L1CA (Japan) |
| 4   | 16    | IMES            | Indoor MEssaging System |
| 5   | 32    | QZSS L1S        | Quasi-Zenith Satellite System L1S (Japan) |
| 6   | 64    | BeiDou          | BeiDou Navigation Satellite System (China) |
| 7   | 128   | Galileo         | European global satellite-based navigation system |

- 1: GPS only
- 2: GLONASS only
- 3: GPS + GLONASS (default)
- 5: GPS + QZSS
- 7: GPS + GLONASS + QZSS
- 71: GPS + GLONASS + QZSS + GALILEO + BEIDOU

## Error Handling

Errors are reported in JSON format:

```json
{
  "status": {
    "code": -1,
    "msg": "Error message"
  }
}
```

## Implementation Details

- The daemon runs as a background process when started
- PID is stored in `/mnt/spif/gnssdaemon.pid`
- Uses signal handling for clean shutdown
- Outputs data to both stdout and `/dev/ttyACM0` for integration with other systems

## Dependencies

- NuttX operating system
- CXD56 GNSS driver

## Troubleshooting

If the daemon fails to start or crashes:
1. Check if another instance is already running
2. Verify the GNSS device is available at `/dev/gps`
3. Ensure the TTY device is available at `/dev/ttyACM0`
4. Check system logs for error messages
5. If experiencing memory issues, consider reducing the polling frequency

## License

This software is provided under the [license terms of the project].
