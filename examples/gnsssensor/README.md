
# GNSS Daemon

## Overview

The GNSS daemon is a continuous monitoring service for Global Navigation Satellite System (GNSS) data. It provides functionality to start and stop GNSS monitoring and retrieve positioning information.

## Features

- Start/stop continuous GNSS monitoring
- Single-shot GNSS data retrieval
- JSON-formatted output for easy integration with other systems
- Configurable polling interval
- Support for multiple satellite systems (GPS, GLONASS, etc.)

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
    "altitude": 123.45
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
    "fix": 0
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
    "altitude": 123.45
  },
  "status": {
    "code": 0,
    "msg": "Position fixed"
  }
}
```

Error responses:

```json
{
  "status": {
    "code": -1,
    "msg": "Error message"
  }
}
```

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

## License

This software is provided under the [license terms of the project].
