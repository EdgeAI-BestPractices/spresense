# ahrs_imusensor

## Overview

`ahrs_imusensor` is a command-line tool that uses the built-in IMU (Inertial Measurement Unit) sensor of the Spresense board to calculate and display the device's attitude (roll, pitch, yaw) in real-time. It employs the Madgwick algorithm to provide high-precision attitude estimation from accelerometer and gyroscope data.

## Features

- Real-time calculation of device attitude (roll, pitch, and yaw angles)
- Quaternion representation of attitude
- Support for both single measurement and continuous polling modes
- JSON-formatted output for easy integration with other applications
- Customizable sampling rate, sensor range, and filter parameters

## Usage

### Basic Usage

```
ahrs_imusensor [command] [options]
```

### Commands

- `start`: Continuously poll sensor data at specified interval
- `stop`: Stop sensor polling
- `(none)`: Read sensor data once

### Options

- `-r RATE`: Specify sample rate (Hz) (15, 30, 60, 120, 240, 480, 960, 1920)
- `-a RANGE`: Specify accelerometer dynamic range (2, 4, 8, 16)
- `-g RANGE`: Specify gyroscope dynamic range (125, 250, 500, 1000, 2000, 4000)
- `-f FIFO`: Specify FIFO threshold (1, 2, 3, 4)
- `-t MS`: Specify polling interval in milliseconds (for poll mode)
- `-i ID`: Specify request ID (for poll mode)
- `-b BETA`: Specify Madgwick filter beta parameter (default: 0.1)
- `-h`: Show help message

## Examples

### Single Measurement

```
nsh> ahrs_imusensor
{"data":{"roll":1.23,"pitch":-0.45,"yaw":178.56,"qw":0.9876,"qx":0.1234,"qy":-0.0123,"qz":0.0456},"status":{"code":0,"msg":"AHRS sensor data read successfully"},"config":{"samplerate":60,"accel_range":2,"gyro_range":125,"fifo":1,"beta":0.1000}}
```

### Start Continuous Polling (1-second interval)

```
nsh> ahrs_imusensor start -t 1000
{"status":{"code":0,"msg":"AHRS sensor polling started successfully"}}
```

### Start Continuous Polling (Custom Settings)

```
nsh> ahrs_imusensor start -r 120 -a 4 -g 500 -b 0.05 -t 500
{"status":{"code":0,"msg":"AHRS sensor polling started successfully"}}
```

### Stop Continuous Polling

```
nsh> ahrs_imusensor stop
{"status":{"code":0,"msg":"AHRS sensor polling stopped successfully"}}
```

## Output Data

The output is in JSON format and includes the following information:

- **Attitude Data**:
  - `roll`: Rotation around X-axis (degrees)
  - `pitch`: Rotation around Y-axis (degrees)
  - `yaw`: Rotation around Z-axis (degrees)

- **Quaternion**:
  - `qw`, `qx`, `qy`, `qz`: The four components of the quaternion representing attitude

- **Configuration Information**:
  - `samplerate`: Sampling rate (Hz)
  - `accel_range`: Accelerometer measurement range (G)
  - `gyro_range`: Gyroscope measurement range (degrees/second)
  - `fifo`: FIFO threshold
  - `beta`: Madgwick filter beta parameter

## Technical Details

This tool uses Madgwick's algorithm for attitude estimation. This algorithm fuses accelerometer and gyroscope data to provide stable attitude estimation.

The beta parameter controls the weighting between accelerometer measurements and gyroscope integration. Higher values give more weight to the accelerometer, while lower values give more weight to the gyroscope.

## Dependencies

- Spresense SDK
- MadgwickAHRS library

## Limitations

- No magnetometer data is used, so absolute heading (yaw angle relative to true north) is not provided.
- During extended use, gyro drift may cause gradual accumulation of errors.

## License

This software is distributed under the copyright of Sony Semiconductor Solutions Corporation. Please refer to the license terms in the source code for details.
