#include <nuttx/config.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <poll.h>
#include <string.h>
#include <nuttx/sensors/cxd5602pwbimu.h>
#include "MadgwickAHRS.h"

#define IMU_BOARD_PATH      "/dev/imu0"
#define TTY_OUTPUT_PATH     "/dev/ttyACM0"
#define SENSOR_PID_FILE     "/mnt/spif/ahrssensor.pid"

static volatile int g_ahrs_running = 1;

static void signal_handler(int signo) {
  g_ahrs_running = 0;
}

static void show_usage(const char *progname) {
  printf("Usage: %s [command] [options]\n", progname);
  printf("Commands:\n");
  printf("  start     Continuously poll sensor data at specified interval\n");
  printf("  stop      Stop sensor polling\n");
  printf("  (none)    Read sensor data once\n");
  printf("Options:\n");
  printf("  -r RATE   Specify sample rate (Hz) (15, 30, 60, 120, 240, 480, 960, 1920)\n");
  printf("  -a RANGE  Specify accelerometer dynamic range (2, 4, 8, 16)\n");
  printf("  -g RANGE  Specify gyroscope dynamic range (125, 250, 500, 1000, 2000, 4000)\n");
  printf("  -f FIFO   Specify FIFO threshold (1, 2, 3, 4)\n");
  printf("  -t MS     Specify polling interval in milliseconds (for poll mode)\n");
  printf("  -i ID     Specify request ID (for poll mode)\n");
  printf("  -b BETA   Specify Madgwick filter beta parameter (default: 0.1)\n");
  printf("  -h        Show this help message\n");
}

static int start_sensing(int fd, int rate, int adrange, int gdrange, int nfifos) {
  cxd5602pwbimu_range_t range;
  int ret;

  /*
   * Set sampling rate. Available values (Hz) are below.
   *
   * 15 (default), 30, 60, 120, 240, 480, 960, 1920
   */

  ret = ioctl(fd, SNIOC_SSAMPRATE, rate);
  if (ret) {
    printf("{\"status\":{\"code\":%d,\"msg\":\"Failed to set sampling rate\"}}\n", ret);
    return ret;
  }

  /*
   * Set dynamic ranges for accelerometer and gyroscope.
   * Available values are below.
   *
   * accel: 2 (default), 4, 8, 16
   * gyro: 125 (default), 250, 500, 1000, 2000, 4000
   */

  range.accel = adrange;
  range.gyro = gdrange;
  ret = ioctl(fd, SNIOC_SDRANGE, (unsigned long)(uintptr_t)&range);
  if (ret) {
    printf("{\"status\":{\"code\":%d,\"msg\":\"Failed to set dynamic range\"}}\n", ret);
    return ret;
  }

  /*
   * Set hardware FIFO threshold.
   * Increasing this value will reduce the frequency with which data is
   * received.
   */

  ret = ioctl(fd, SNIOC_SFIFOTHRESH, nfifos);
  if (ret) {
    printf("{\"status\":{\"code\":%d,\"msg\":\"Failed to set FIFO threshold\"}}\n", ret);
    return ret;
  }

  /*
   * Start sensing, user can not change the all of configurations.
   */

  ret = ioctl(fd, SNIOC_ENABLE, 1);
  if (ret) {
    printf("{\"status\":{\"code\":%d,\"msg\":\"Failed to enable sensor\"}}\n", ret);
    return ret;
  }

  return 0;
}

// Save daemon PID to file
static int save_daemon_pid(pid_t pid) {
  FILE *fp = fopen(SENSOR_PID_FILE, "w");
  if (!fp) return -errno;

  fprintf(fp, "%d", pid);
  fclose(fp);

  return 0;
}

// Load daemon PID from file
static pid_t load_daemon_pid(void) {
  FILE *fp = fopen(SENSOR_PID_FILE, "r");
  if (!fp) return -1;

  pid_t pid;
  if (fscanf(fp, "%d", &pid) != 1) {
    fclose(fp);
    return -1;
  }

  fclose(fp);
  return pid;
}

// Check if daemon is running
static int is_daemon_running(void) {
  pid_t pid = load_daemon_pid();
  if (pid <= 0) return 0;

  if (kill(pid, 0) == 0) {
    return 1;  // Process exists
  }
  return 0;  // Process does not exist
}

static int drop_50msdata(int fd, int samprate, cxd5602pwbimu_data_t *imu)
{
  int cnt = samprate / 20; /* data size of 50ms */

  if (cnt == 0) cnt = 1;

  while (cnt)
    {
      read(fd, imu, sizeof(imu[0]));
      cnt--;
    }

  return 0;
}

static void run_poll_mode(int samplerate, int adrange, int gdrange, int nfifos, int interval_ms, int request_id, float beta) {
  int fd_sensor, fd_tty;
  struct pollfd fds[1];
  int ret;
  cxd5602pwbimu_data_t data;
  char data_json[256];
  char config_json[128];
  struct ahrs_out_s ahrs;
  float euler[3] = {0.0f, 0.0f, 0.0f};
  unsigned long last_output_time = 0;
  struct timespec current_time;

  // Initialize AHRS
  INIT_AHRS(&ahrs, beta, (float)samplerate);

  // Set up signal handler
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  // Open sensor device
  fd_sensor = open(IMU_BOARD_PATH, O_RDONLY);
  if (fd_sensor < 0) {
    printf("{\"status\":{\"code\":%d,\"msg\":\"Failed to open sensor device\"}}\n", -errno);
    exit(1);
  }

  // Initialize sensor
  ret = start_sensing(fd_sensor, samplerate, adrange, gdrange, nfifos);
  if (ret) {
    close(fd_sensor);
    exit(1);
  }

  // Discard first data
  fds[0].fd = fd_sensor;
  fds[0].events = POLLIN;
  ret = poll(fds, 1, 1000);
  if (ret > 0 && fds[0].revents & POLLIN) {
    cxd5602pwbimu_data_t dummy_data;
    ret = read(fd_sensor, &dummy_data, sizeof(dummy_data));
  }

  drop_50msdata(fd_sensor, samplerate, &data);

  // Open TTY device for output
  fd_tty = open(TTY_OUTPUT_PATH, O_WRONLY);
  if (fd_tty < 0) {
    printf("{\"status\":{\"code\":%d,\"msg\":\"Failed to open TTY device\"}}\n", -errno);
    ioctl(fd_sensor, SNIOC_ENABLE, 0);
    close(fd_sensor);
    exit(1);
  }

  // Create config JSON once
  snprintf(config_json, sizeof(config_json),
         "{\"samplerate\":%d,\"accel_range\":%d,\"gyro_range\":%d,\"fifo\":%d,\"beta\":%.4f}",
         samplerate, adrange, gdrange, nfifos, beta);

  // Send initial response to indicate polling has started
  char init_msg[256];
  snprintf(init_msg, sizeof(init_msg),
         "{\"cmd\":\"ahrs_imusensor\",\"type\":\"res\",\"status\":{\"code\":0,\"msg\":\"Polling started\"},\"id\":%d}\n",
         request_id);
  write(fd_tty, init_msg, strlen(init_msg));

  clock_gettime(CLOCK_MONOTONIC, &current_time);
  last_output_time = current_time.tv_sec * 1000 + current_time.tv_nsec / 1000000;

  while (g_ahrs_running) {
    fds[0].fd = fd_sensor;
    fds[0].events = POLLIN;

    ret = poll(fds, 1, 20);  // Short timeout
    if (ret > 0 && (fds[0].revents & POLLIN)) {
      ret = read(fd_sensor, &data, sizeof(data));
      if (ret == sizeof(data)) {
        // Update AHRS
        MadgwickAHRSupdateIMU(&ahrs, data.gx, data.gy, data.gz, data.ax, data.ay, data.az);

        clock_gettime(CLOCK_MONOTONIC, &current_time);
        unsigned long current_ms = current_time.tv_sec * 1000 + current_time.tv_nsec / 1000000;

        if (current_ms - last_output_time >= interval_ms) {
          // Convert quaternion to Euler angles
          quaternion2euler(ahrs.q, euler);

          // Create data JSON
          snprintf(data_json, sizeof(data_json),
                   "{\"roll\":%.2f,\"pitch\":%.2f,\"yaw\":%.2f,\"qw\":%.4f,\"qx\":%.4f,\"qy\":%.4f,\"qz\":%.4f}",
                   euler[0], euler[1], euler[2], ahrs.q[0], ahrs.q[1], ahrs.q[2], ahrs.q[3]);

          // Create full JSON output
          char output[1024];
          snprintf(output, sizeof(output),
                   "{\"cmd\":\"ahrs_imusensor\",\"type\":\"poll\",\"interval\":%d,\"id\":%d,"
                   "\"status\":{\"code\":0,\"msg\":\"\"},\"data\":%s,\"config\":%s}\n",
                   interval_ms, request_id, data_json, config_json);

          // Write directly to ttyACM0
          write(fd_tty, output, strlen(output));

          // Update last output time
          last_output_time = current_ms;
        }
      }
    }
  }
  // Cleanup
  ioctl(fd_sensor, SNIOC_ENABLE, 0);
  close(fd_sensor);
  close(fd_tty);
  unlink(SENSOR_PID_FILE);
}

int main(int argc, FAR char *argv[]) {
  int fd;
  struct pollfd fds[1];
  int ret;
  cxd5602pwbimu_data_t data;
  int opt;
  char data_json[256];
  char config_json[128];
  char *cmd = NULL;
  int interval_ms = 1000;
  int request_id = 0;
  float beta = 0.1f;  // Default Madgwick filter parameter
  struct ahrs_out_s ahrs;
  float euler[3] = {0.0f, 0.0f, 0.0f};

  g_ahrs_running = 1;

  /* Sensing parameters */
  int samplerate = 60;
  int adrange = 2;
  int gdrange = 125;
  int nfifos = 1;

  // Initialize AHRS
  INIT_AHRS(&ahrs, beta, (float)samplerate);

  // Process command argument
  if (argc > 1 && argv[1][0] != '-') {
    cmd = argv[1];
    // Remove command from argument list
    for (int i = 1; i < argc - 1; i++) {
      argv[i] = argv[i + 1];
    }
    argc--;
  }

  // Process option arguments
  optind = 1;
  while ((opt = getopt(argc, argv, "r:a:g:f:t:i:b:h")) != -1) {
    switch (opt) {
        case 'r':
          samplerate = atoi(optarg);
          if (samplerate != 15 && samplerate != 30 && samplerate != 60 &&
              samplerate != 120 && samplerate != 240 && samplerate != 480 &&
              samplerate != 960 && samplerate != 1920) {
            printf("{\"status\":{\"code\":-1,\"msg\":\"Invalid sample rate\"}}\n");
            return 1;
          }
          ahrs.samplerate = samplerate;
          break;
        case 'a':
          adrange = atoi(optarg);
          if (adrange != 2 && adrange != 4 && adrange != 8 && adrange != 16) {
            printf("{\"status\":{\"code\":-1,\"msg\":\"Invalid accelerometer range\"}}\n");
            return 1;
          }
          break;
        case 'g':
          gdrange = atoi(optarg);
          if (gdrange != 125 && gdrange != 250 && gdrange != 500 &&
              gdrange != 1000 && gdrange != 2000 && gdrange != 4000) {
            printf("{\"status\":{\"code\":-1,\"msg\":\"Invalid gyroscope range\"}}\n");
            return 1;
          }
          break;
        case 'f':
          nfifos = atoi(optarg);
          if (nfifos < 1 || 4 < nfifos) {
            printf("{\"status\":{\"code\":-1,\"msg\":\"FIFO threshold must be between 1 and 4\"}}\n");
            return 1;
          }
          break;
        case 't':
          interval_ms = atoi(optarg);
          if (interval_ms < 0) {
            printf("{\"status\":{\"code\":-1,\"msg\":\"Interval must be positive\"}}\n");
            return 1;
          }
          break;
        case 'i':
          request_id = atoi(optarg);
          break;
        case 'b':
          beta = atof(optarg);
          if (beta <= 0.0f || beta > 1.0f) {
            printf("{\"status\":{\"code\":-1,\"msg\":\"Beta must be between 0 and 1\"}}\n");
            return 1;
          }
          ahrs.beta = beta;
          break;
        case 'h':
          show_usage(argv[0]);
          return 0;
        default:
          show_usage(argv[0]);
          return 1;
    }
  }

  // Handle "stop" command
  if (cmd && strcmp(cmd, "stop") == 0) {
    // Check if polling process is running
    pid_t daemon_pid = load_daemon_pid();
    if (daemon_pid <= 0) {
      printf("{\"status\":{\"code\":-1,\"msg\":\"AHRS sensor daemon not running\"}}\n");
      return 1;
    }
    if (kill(daemon_pid, SIGTERM) < 0) {
      ret = -errno;
      printf("{\"status\":{\"code\":%d,\"msg\":\"Failed to stop AHRS sensor daemon\"}}\n", ret);
      return 1;
    }
    usleep(100000);
    unlink(SENSOR_PID_FILE);
    printf("{\"status\":{\"code\":0,\"msg\":\"AHRS sensor polling stopped successfully\"}}\n");
    return 0;
  }
  // Handle "start" command
  else if (cmd && strcmp(cmd, "start") == 0) {
    if (is_daemon_running()) {
      pid_t old_pid = load_daemon_pid();
      kill(old_pid, SIGTERM);
      unlink(SENSOR_PID_FILE);
      usleep(500000);
    }

    pid_t pid = fork();
    if (pid < 0) {
      // Fork failed
      ret = -errno;
      printf("{\"status\":{\"code\":%d,\"msg\":\"Failed to create daemon process\"}}\n", ret);
      return 1;
    }
    else if (pid == 0) {
      // Child process (daemon)

      // Close standard input/output
      close(STDIN_FILENO);
      // Don't close STDOUT/STDERR to allow error reporting
      /* close(STDOUT_FILENO); */
      /* close(STDERR_FILENO); */

      // Save daemon PID
      save_daemon_pid(getpid());

      // Run polling mode
      run_poll_mode(samplerate, adrange, gdrange, nfifos, interval_ms, request_id, beta);
      exit(0);
    }
    else {
      // Save daemon PID
      save_daemon_pid(pid);
      printf("{\"status\":{\"code\":0,\"msg\":\"AHRS sensor polling started successfully\"}}\n");
      return 0;
    }
  }
  // Handle data reading (default mode)
  else {
    fd = open(IMU_BOARD_PATH, O_RDONLY);
    if (fd < 0) {
      ret = -errno;
      printf("{\"status\":{\"code\":%d,\"msg\":\"Failed to open device\"}}\n", ret);
      return 1;
    }
    ret = ioctl(fd, SNIOC_ENABLE, 1);
    if (ret < 0) {
      printf("{\"status\":{\"code\":%d,\"msg\":\"Failed to enable sensor\"}}\n", -errno);
      close(fd);
      return 1;
    }
    // Read data
    fds[0].fd = fd;
    fds[0].events = POLLIN;

    drop_50msdata(fd, samplerate, &data);

    ret = poll(fds, 1, 1000);
    if (ret <= 0) {
      if (ret == 0) {
        printf("{\"status\":{\"code\":-1,\"msg\":\"Timeout waiting for sensor data\"}}\n");
      } else {
        printf("{\"status\":{\"code\":%d,\"msg\":\"Poll failed\"}}\n", -errno);
      }
      ioctl(fd, SNIOC_ENABLE, 0);
      close(fd);
      return 1;
    }
    if (fds[0].revents & POLLIN) {
      ret = read(fd, &data, sizeof(data));
      if (ret != sizeof(data)) {
        printf("{\"status\":{\"code\":-1,\"msg\":\"Failed to read data\"}}\n");
        ioctl(fd, SNIOC_ENABLE, 0);
        close(fd);
        return 1;
      }

      // Update AHRS
      MadgwickAHRSupdateIMU(&ahrs, data.gx, data.gy, data.gz, data.ax, data.ay, data.az);

      // Convert quaternion to Euler angles
      quaternion2euler(ahrs.q, euler);

      // Create data JSON
      snprintf(data_json, sizeof(data_json),
               "{\"roll\":%.2f,\"pitch\":%.2f,\"yaw\":%.2f,\"qw\":%.4f,\"qx\":%.4f,\"qy\":%.4f,\"qz\":%.4f}",
               euler[0], euler[1], euler[2], ahrs.q[0], ahrs.q[1], ahrs.q[2], ahrs.q[3]);

      // Create config JSON
      snprintf(config_json, sizeof(config_json),
               "{\"samplerate\":%d,\"accel_range\":%d,\"gyro_range\":%d,\"fifo\":%d,\"beta\":%.4f}",
               samplerate, adrange, gdrange, nfifos, beta);

      printf("{\"data\":%s,\"status\":{\"code\":0,\"msg\":\"AHRS sensor data read successfully\"},\"config\":%s}\n",
             data_json, config_json);
    }
    ioctl(fd, SNIOC_ENABLE, 0);
    close(fd);
  }

  return 0;
}
