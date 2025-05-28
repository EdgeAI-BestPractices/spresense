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

#define IMU_BOARD_PATH      "/dev/imu0"
#define TTY_OUTPUT_PATH     "/dev/ttyACM0"
#define SENSOR_PID_FILE     "/mnt/spif/imusensor.pid"

static volatile int g_imu_running = 1;

static void signal_handler(int signo) {
  g_imu_running = 0;
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

static void run_poll_mode(int samplerate, int adrange, int gdrange, int nfifos, int interval_ms, int request_id) {
  int fd_sensor, fd_tty;
  struct pollfd fds[1];
  int ret;
  cxd5602pwbimu_data_t data;
  char data_json[256];
  char config_json[128];

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
         "{\"samplerate\":%d,\"accel_range\":%d,\"gyro_range\":%d,\"fifo\":%d}",
         samplerate, adrange, gdrange, nfifos);

  // Send initial response to indicate polling has started
  char init_msg[256];
  snprintf(init_msg, sizeof(init_msg),
         "{\"cmd\":\"imusensor\",\"type\":\"res\",\"status\":{\"code\":0,\"msg\":\"Polling started\"},\"id\":%d}\n",
         request_id);
  write(fd_tty, init_msg, strlen(init_msg));
  while (g_imu_running) {
    fds[0].fd = fd_sensor;
    fds[0].events = POLLIN;

    ret = poll(fds, 1, 100);  // Short timeout
    if (ret > 0 && (fds[0].revents & POLLIN)) {
      ret = read(fd_sensor, &data, sizeof(data));
      if (ret == sizeof(data)) {
        // Create data JSON
        snprintf(data_json, sizeof(data_json),
                 "{\"x\":%.8f,\"y\":%.8f,\"z\":%.8f,\"gx\":%.8f,\"gy\":%.8f,\"gz\":%.8f}",
                 data.ax, data.ay, data.az, data.gx, data.gy, data.gz);

        // Create full JSON output
        char output[1024];
        snprintf(output, sizeof(output),
                 "{\"cmd\":\"imusensor\",\"type\":\"poll\",\"interval\":%d,\"id\":%d,"
                 "\"status\":{\"code\":0,\"msg\":\"\"},\"data\":%s,\"config\":%s}\n",
                 interval_ms, request_id, data_json, config_json);

        // Write directly to ttyACM0
        write(fd_tty, output, strlen(output));
      }
    }
    usleep(interval_ms * 1000);
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

  g_imu_running = 1;

  /* Sensing parameters */
  int samplerate = 60;
  int adrange = 2;
  int gdrange = 125;
  int nfifos = 1;

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
  while ((opt = getopt(argc, argv, "r:a:g:f:t:i:h")) != -1) {
    switch (opt) {
        case 'r':
          samplerate = atoi(optarg);
          if (samplerate != 15 && samplerate != 30 && samplerate != 60 &&
              samplerate != 120 && samplerate != 240 && samplerate != 480 &&
              samplerate != 960 && samplerate != 1920) {
            printf("{\"status\":{\"code\":-1,\"msg\":\"Invalid sample rate\"}}\n");
            return 1;
          }
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
      printf("{\"status\":{\"code\":-1,\"msg\":\"Sensor daemon not running\"}}\n");
      return 1;
    }
    if (kill(daemon_pid, SIGTERM) < 0) {
      ret = -errno;
      printf("{\"status\":{\"code\":%d,\"msg\":\"Failed to stop sensor daemon\"}}\n", ret);
      return 1;
    }
    usleep(100000);
    unlink(SENSOR_PID_FILE);
    printf("{\"status\":{\"code\":0,\"msg\":\"Sensor polling stopped successfully\"}}\n");
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
      // // Fork failed
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
      run_poll_mode(samplerate, adrange, gdrange, nfifos, interval_ms, request_id);
      exit(0);
    }
    else {
      // Save daemon PID
      save_daemon_pid(pid);
      printf("{\"status\":{\"code\":0,\"msg\":\"Sensor polling started successfully\"}}\n");
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

      // Create data JSON
      snprintf(data_json, sizeof(data_json),
               "{\"x\":%.8f,\"y\":%.8f,\"z\":%.8f,\"gx\":%.8f,\"gy\":%.8f,\"gz\":%.8f}",
               data.ax, data.ay, data.az, data.gx, data.gy, data.gz);

      // Create config JSON
      snprintf(config_json, sizeof(config_json),
               "{\"samplerate\":%d,\"accel_range\":%d,\"gyro_range\":%d,\"fifo\":%d}",
               samplerate, adrange, gdrange, nfifos);

      printf("{\"data\":%s,\"status\":{\"code\":0,\"msg\":\"Sensor data read successfully\"},\"config\":%s}\n",
             data_json, config_json);
    }
    ioctl(fd, SNIOC_ENABLE, 0);
    close(fd);
  }

  return 0;
}
