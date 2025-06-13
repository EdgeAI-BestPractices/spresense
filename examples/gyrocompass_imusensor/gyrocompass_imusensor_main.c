#include <nuttx/config.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <poll.h>
#include <string.h>
#include <imu_utils/imu_utils.h>

#include "../gyrocompass_pwbimu/gyrocompass.h"

#define TTY_OUTPUT_PATH     "/dev/ttyACM0"
#define SENSOR_PID_FILE     "/mnt/spif/gyrocompass_imusensor.pid"

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
  printf("  -r RATE   Specify sample rate (Hz) (15, 30, 60, 120, 240, 480, 960, 1920 (default))\n");
  printf("  -a RANGE  Specify accelerometer dynamic range (2, 4 (default), 8, 16)\n");
  printf("  -g RANGE  Specify gyroscope dynamic range (125 (default), 250, 500, 1000, 2000, 4000)\n");
  printf("  -f FIFO   Specify FIFO threshold (1 (default), 2, 3, 4)\n");
  printf("  -i ID     Specify request ID (for poll mode)\n");
  printf("  -t TIME   Specify data collecting time for each posture in milliseconds (default: 1000ms)\n");
  printf("  -p NUM    Specify number of previous posture datasets saved in memory (default: 8)\n");
  printf("  -d TIME   Specify time to discard data before collecting posture data in milliseconds (default: 50ms)\n");
  printf("  -h        Show this help message\n");
}

static int capture_imudata(int fd, int nfifos, struct dvec3_s *avrg,
  cxd5602pwbimu_data_t *imudata, int capcnt) {
  memset(avrg, 0, sizeof(struct dvec3_s));

  for (int i = 0; i < capcnt; i += nfifos) {
    if (!pwbimu_read_imudata(fd, nfifos, imudata)) {
      return -1;
    }
    for (int j = 0; j < nfifos; j++) {
      avrg->x += (imudata + j)->gx;
      avrg->y += (imudata + j)->gy;
      avrg->z += (imudata + j)->gz;
    }
  }

  avrg->x /= (double) capcnt;
  avrg->y /= (double) capcnt;
  avrg->z /= (double) capcnt;

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
static bool is_daemon_running(void) {
  pid_t pid = load_daemon_pid();
  if (pid <= 0) return false;

  if (kill(pid, 0) == 0) {
    return true;  // Process exists
  }
  return false;  // Process does not exist
}

static void run_poll_mode(int samplerate, int adrange, int gdrange, int nfifos, int request_id, int collect_time, int posture_cnt, int discard_time) {
  int ret = 0;
  int fd_sensor = -1;
  int fd_tty = 0;
  cxd5602pwbimu_data_t *imudata = NULL;
  struct dvec3_s *gyrodata = NULL;
  char msg[256];

  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  fd_sensor = pwbimu_start_sensing(samplerate, adrange, gdrange, nfifos);
  if (fd_sensor < 0) {
    ret = -errno;
    printf("{\"status\":{\"code\":%d,\"msg\":\"Failed to open sensor device\"}}\n", ret);
    goto cleanup;
  }

  fd_tty = open(TTY_OUTPUT_PATH, O_WRONLY);
  if (fd_tty < 0) {
    ret = -errno;
    printf("{\"status\":{\"code\":%d,\"msg\":\"Failed to open TTY device\"}}\n", ret);
    goto cleanup;
  }

  imudata = malloc(sizeof(cxd5602pwbimu_data_t) *  nfifos);
  if (!imudata) {
    ret = -errno;
    printf("{\"status\":{\"code\":%d,\"msg\":\"Failed to open allocate memory for PMU data\"}}\n", ret);
    goto cleanup;
  }

  gyrodata = malloc(sizeof(struct dvec3_s) * posture_cnt);
  if (!gyrodata) {
    ret = -errno;
    printf("{\"status\":{\"code\":%d,\"msg\":\"Failed to open allocate memory for posture data\"}}\n", ret);
    goto cleanup;
  }
  memset(gyrodata, 0, sizeof(struct dvec3_s) * posture_cnt);

  if (discard_time > 0) {
    pwbimu_drop_data(fd_sensor, samplerate, discard_time, nfifos, imudata);
  }

  // Send initial response to indicate polling has started
  snprintf(msg, sizeof(msg),
    "{\"cmd\":\"imusensor\",\"type\":\"res\",\"status\":{\"code\":0,\"msg\":\"Polling started\"},\"id\":%d}\n",
    request_id);
  write(fd_tty, msg, strlen(msg));

  int capcnt = samplerate * collect_time / 1000;
  int current_posture_idx = 0;
  char config_json[128];
  snprintf(config_json, sizeof(config_json),
    "{\"samplerate\":%d,\"accel_range\":%d,\"gyro_range\":%d,\"fifo\":%d}",
    samplerate, adrange, gdrange, nfifos);
  while (g_imu_running) {
    struct dvec3_s gyro;
    if (capture_imudata(fd_sensor, nfifos, &gyro, imudata, capcnt) < 0) {
      snprintf(msg, sizeof(msg),
        "{\"status\":{\"code\":%d,\"msg\":\"Failed to calculate bias\",\"id\":%d}}\n",
        ret, request_id);
      write(fd_tty, msg, strlen(msg));
      continue;
    }

    struct dvec3_s old_gyro;
    memcpy(&old_gyro, gyrodata + current_posture_idx, sizeof(struct dvec3_s));
    memcpy(gyrodata + current_posture_idx, &gyro, sizeof(struct dvec3_s));

    struct dvec3_s bias_vec;
    if ((ret = calc_bias_circlefitting(gyrodata, posture_cnt, &bias_vec)) < 0) {
      // The current data is not valid, thus restore it to the previous one
      memcpy(gyrodata + current_posture_idx, &old_gyro, sizeof(struct dvec3_s));
      snprintf(msg, sizeof(msg),
        "{\"status\":{\"code\":%d,\"msg\":\"Failed to calculate bias\",\"id\":%d}}\n",
        ret, request_id);
      write(fd_tty, msg, strlen(msg));
      continue;
    }

    double heading = calc_device_heading2d(gyro.x - bias_vec.x, gyro.y - bias_vec.y);
    snprintf(msg, sizeof(msg),
      "{\"data\":{\"heading\":%f},\"status\":{\"code\":0,\"msg\":\"Success\"},\"config\":%s,\"id\":%d}\n",
      heading, config_json, request_id);
      write(fd_tty, msg, strlen(msg));

    current_posture_idx++;
    if (current_posture_idx >= posture_cnt) {
      current_posture_idx = 0;
    }
  }

  cleanup:
  if (fd_sensor >= 0) {
    pwbimu_terminate(fd_sensor);
  }
  if (fd_tty >= 0) {
    close(fd_tty);
  }
  if (imudata) {
    free(imudata);
  }
  if (gyrodata) {
    free(gyrodata);
  }
  unlink(SENSOR_PID_FILE);
  exit(ret);
}

int main(int argc, FAR char *argv[]) {
  int ret = 0;
  int opt;
  char *cmd = NULL;
  int request_id = 0;
  int collect_time = 1000;
  int posture_cnt = 8;
  int discard_time = 50;

  g_imu_running = 1;

  /* Sensing parameters */
  int samplerate = 1920;
  int adrange = 4;
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
  while ((opt = getopt(argc, argv, "r:a:g:f:t:i:c:d:h")) != -1) {
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
        case 'i':
          request_id = atoi(optarg);
          break;
        case 't':
          collect_time = atoi(optarg);
          if (collect_time <= 0) {
            printf("{\"status\":{\"code\":-1,\"msg\":\"Posture data collecting time must be positive\"}}\n");
            return 1;
          }
          break;
        case 'p':
          posture_cnt = atoi(optarg);
          if (posture_cnt <= 0) {
            printf("{\"status\":{\"code\":-1,\"msg\":\"Posture dataset number must be positive\"}}\n");
            return 1;
          }
          break;
        case 'd':
          discard_time = atoi(optarg);
          if (discard_time < 0) {
            printf("{\"status\":{\"code\":-1,\"msg\":\"Data discard time must be nonnegative\"}}\n");
            return 1;
          }
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
      // Fork failed
      ret = -errno;
      printf("{\"status\":{\"code\":%d,\"msg\":\"Failed to create daemon process\"}}\n", ret);
      return 1;
    } else if (pid == 0) {
      // Child process (daemon)

      // Close standard input/output
      close(STDIN_FILENO);
      // Don't close STDOUT/STDERR to allow error reporting
      /* close(STDOUT_FILENO); */
      /* close(STDERR_FILENO); */

      // Save daemon PID
      save_daemon_pid(getpid());

      // Run polling mode
      run_poll_mode(samplerate, adrange, gdrange, nfifos, request_id, collect_time, posture_cnt, discard_time);
      exit(0);
    } else {
      // Save daemon PID
      save_daemon_pid(pid);
      printf("{\"status\":{\"code\":0,\"msg\":\"Sensor polling started successfully\"}}\n");
      return 0;
    }
  } else {
    // Handle data reading (default mode)
    int fd = -1;
    cxd5602pwbimu_data_t *data = NULL;

    fd = pwbimu_start_sensing(samplerate, adrange, gdrange, nfifos);
    if (fd < 0) {
      ret = 1;
      goto cleanup;
    }

    data = malloc(sizeof(cxd5602pwbimu_data_t) *  nfifos);
    if (!data) {
      ret = -errno;
      printf("{\"status\":{\"code\":%d,\"msg\":\"Failed to open allocate memory for PMU data\"}}\n", ret);
      goto cleanup;
    }

    if (discard_time > 0) {
      pwbimu_drop_data(fd, samplerate, discard_time, nfifos, data);
    }

    int capcnt = samplerate * collect_time / 1000;
    struct dvec3_s gyro;
    if (capture_imudata(fd, nfifos, &gyro, data, capcnt) < 0) {
      ret = -errno;
      printf("{\"status\":{\"code\":%d,\"msg\":\"Failed to collect gyro information\"}}\n", ret);
      goto cleanup;
    }

    struct dvec3_s bias_vec;
    if ((ret = calc_bias_circlefitting(&gyro, 1, &bias_vec)) < 0) {
      printf("{\"status\":{\"code\":%d,\"msg\":\"Failed to calculate bias\"}}\n", ret);
      goto cleanup;
    }

    double heading = calc_device_heading2d(gyro.x - bias_vec.x, gyro.y - bias_vec.y);

    char config_json[128];
    snprintf(config_json, sizeof(config_json),
      "{\"samplerate\":%d,\"accel_range\":%d,\"gyro_range\":%d,\"fifo\":%d}",
      samplerate, adrange, gdrange, nfifos);
    printf("{\"data\":{\"heading\":%f},\"status\":{\"code\":0,\"msg\":\"Success\"},\"config\":%s}\n",
      heading, config_json);

    cleanup:
    if (fd >= 0) {
      pwbimu_terminate(fd);
    }
    if (data) {
      free(data);
    }
  }

  return ret;
}
