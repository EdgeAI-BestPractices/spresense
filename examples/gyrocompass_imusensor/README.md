# gyrocompass_imusensor

## Overview

`gyrocompass_imusensor` is a command-line tool that uses the built-in IMU (Inertial Measurement Unit) sensor of the Spresense board to calculate and display the device's heading direction with gyrocompass.

## Features

- Real-time calculation of device heading direction (N: 0, E: 90, S: 180, W: 270 etc.)
- Support for both single measurement and continuous polling modes
- JSON-formatted output for easy integration with other applications
- Customizable sampling rate, sensor range, and posture sampling parameters

## Usage

### Basic Usage

```shell
gyrocompass_imusensor [command] [options]
```

### Commands

- `start`: Continuously poll sensor data at specified interval
- `stop`: Stop sensor polling
- `(none)`: Read sensor data once

### Options

- `-r RATE`: Specify sample rate (Hz) (15, 30, 60, 120, 240, 480, 960, 1920 (default))
- `-a RANGE`: Specify accelerometer dynamic range (2, 4 (default), 8, 16)
- `-g RANGE`: Specify gyroscope dynamic range (125 (default), 250, 500, 1000, 2000, 4000)
- `-f FIFO`: Specify FIFO threshold (1 (default), 2, 3, 4)
- `-i ID`: Specify request ID (for poll mode)
- `-t TIME`: Specify data collecting time for each posture in milliseconds (default: 1000ms)
- `-p NUM`: Specify number of previous posture datasets saved in memory (default: 8)
- `-d TIME`: Specify time to discard data before collecting posture data in milliseconds (default: 50ms)
- `-h`: Show help message

## Examples

### Single Measurement

```shell
nsh> gyrocompass_imusensor
{"data":{"heading":315.000000},"status":{"code":0,"msg":"Success"},"config":{"samplerate":1920,"accel_range":4,"gyro_range":125,"fifo":1}}
```

Since the default mode only obtains data from a single posture, with high probability, it will return

```shell
nsh> gyrocompass_imusensor
{"status":{"code":-1,"msg":"Failed to calculate bias"}}
```

Even if it returns success, it's likely the heading direction is not accurate.

The Default mode is only for testing purposes.

### Start Continuous Polling (collecting 1-second data from each posture)

Run `sercon` before starting polling mode.

```shell
nsh> gyrocompass_imusensor start -t 1000
{"status":{"code":0,"msg":"Sensor polling started successfully"}}
```

### Start Continuous Polling (Custom Settings)

```shell
nsh> gyrocompass_imusensor start -t 500 -p 32 -d 100
{"status":{"code":0,"msg":"Sensor polling started successfully"}}
```

### Stop Continuous Polling

```shell
nsh> gyrocompass_imusensor stop
{"status":{"code":0,"msg":"Sensor polling stopped successfully"}}
```

## Output Data

The output is in JSON format and includes the following information:

- **Data**:
  - `heading`: the heading direction. 0 for north, 90 for east, etc.

- **Quaternion**:
  - `qw`, `qx`, `qy`, `qz`: The four components of the quaternion representing attitude

- **Configuration Information**:
  - `samplerate`: Sampling rate (Hz)
  - `accel_range`: Accelerometer measurement range (G)
  - `gyro_range`: Gyroscope measurement range (degrees/second)
  - `fifo`: FIFO threshold
  - `beta`: Madgwick filter beta parameter

## Dependencies

- Spresense SDK
- imu_utils

## Limitations

- The current direction is calculated also from previous data, thus it takes several seconds before the data become accurate.
- Since it takes time to collect data for each position, it is not possible to obtain realtime data.

## License

This software is distributed under the copyright of Sony Semiconductor Solutions Corporation. Please refer to the license terms in the source code for details.
