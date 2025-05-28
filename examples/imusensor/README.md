
# IMU Sensor Command for Spresense

## Overview

The `imusensor` command is a utility for reading data from the IMU (Inertial Measurement Unit) sensor on the Spresense board. It retrieves accelerometer and gyroscope data and outputs it in JSON format.

## Features

- Single sensor data reading
- Continuous polling mode (runs as a daemon process)
- Configuration of sampling rate, accelerometer range, gyroscope range, and FIFO threshold
- JSON formatted output

## Usage

```
imusensor [command] [options]
```

### Commands

- `start` - Continuously poll sensor data at specified interval
- `stop` - Stop sensor polling
- `(none)` - Read sensor data once

### Options

- `-r RATE` - Specify sample rate (Hz) (15, 30, 60, 120, 240, 480, 960, 1920)
- `-a RANGE` - Specify accelerometer dynamic range (2, 4, 8, 16)
- `-g RANGE` - Specify gyroscope dynamic range (125, 250, 500, 1000, 2000, 4000)
- `-f FIFO` - Specify FIFO threshold (1, 2, 3, 4)
- `-t MS` - Specify polling interval in milliseconds (for poll mode)
- `-i ID` - Specify request ID (for poll mode)
- `-h` - Show help message

## Examples

### Reading Sensor Data Once

```
imusensor
```

Example output:
```json
{"data":{"x":0.18192700,"y":0.78120422,"z":9.76083755,"gx":-0.00013202,"gy":0.00018442,"gz":-0.00015234},"status":{"code":0,"msg":"Sensor data read successfully"},"config":{"samplerate":60,"accel_range":2,"gyro_range":125,"fifo":1}}
```

### Reading Sensor Data with Custom Configuration

```
imusensor -r 120 -a 4 -g 250
```

### Starting Polling Mode

```
imusensor start -t 500 -i 1234
```

Example output:
```json
{"status":{"code":0,"msg":"Sensor polling started successfully"}}
```

In polling mode, sensor data will be output to `/dev/ttyACM0` at the specified interval (500 milliseconds in this example).

### Stopping Polling Mode

```
imusensor stop
```

Example output:
```json
{"status":{"code":0,"msg":"Sensor polling stopped successfully"}}
```

## Output Format

### Single Reading Mode

This is only the output when operating with a single command, and other data is supplemented by the serial_controller.

```json
{
  "data": {
    "x": 0.18192700,    // X-axis acceleration (m/s²)
    "y": 0.78120422,    // Y-axis acceleration (m/s²)
    "z": 9.76083755,    // Z-axis acceleration (m/s²)
    "gx": -0.00013202,  // X-axis angular velocity (rad/s)
    "gy": 0.00018442,   // Y-axis angular velocity (rad/s)
    "gz": -0.00015234   // Z-axis angular velocity (rad/s)
  },
  "status": {
    "code": 0,          // Status code (0=success)
    "msg": "Sensor data read successfully"  // Status message
  },
  "config": {
    "samplerate": 60,   // Sampling rate (Hz)
    "accel_range": 2,   // Accelerometer range (g)
    "gyro_range": 125,  // Gyroscope range (dps)
    "fifo": 1           // FIFO threshold
  }
}
```

### Polling Mode

In polling mode, since it operates independently from the serial_controller when executing imusensor start, the JSON format is slightly different.

```json
{
  "cmd": "imusensor",   // Command name
  "type": "poll",       // Response type
  "interval": 500,      // Polling interval (ms)
  "id": 1234,           // Request ID
  "status": {
    "code": 0,          // Status code
    "msg": ""           // Status message
  },
  "data": {
    "x": 0.18192700,    // X-axis acceleration
    "y": 0.78120422,    // Y-axis acceleration
    "z": 9.76083755,    // Z-axis acceleration
    "gx": -0.00013202,  // X-axis angular velocity
    "gy": 0.00018442,   // Y-axis angular velocity
    "gz": -0.00015234   // Z-axis angular velocity
  },
  "config": {
    "samplerate": 60,   // Sampling rate
    "accel_range": 2,   // Accelerometer range
    "gyro_range": 125,  // Gyroscope range
    "fifo": 1           // FIFO threshold
  }
}
```

## Error Handling

When a command fails, the `status.code` field in the JSON response will be set to a non-zero value, and the `status.msg` field will contain an error message.

Example:
```json
{"status":{"code":-1,"msg":"Invalid sample rate"}}
```

## Device Paths

- IMU sensor: `/dev/imu0`
- Output TTY: `/dev/ttyACM0`
