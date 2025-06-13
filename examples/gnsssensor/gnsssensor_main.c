/****************************************************************************
 * gnss_daemon.c
 *
 * GNSS continuous monitoring daemon with start/stop functionality
 ****************************************************************************/

#include <nuttx/config.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arch/chip/gnss.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define GNSS_POLL_FD_NUM          1
#define GNSS_POLL_TIMEOUT_FOREVER -1
#define MY_GNSS_SIG               18
#define GNSS_DEVICE_PATH          "/dev/gps"
#define TTY_OUTPUT_PATH           "/dev/ttyACM0"
#define GNSS_PID_FILE             "/mnt/spif/gnssdaemon.pid"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct cxd56_gnss_dms_s
{
  int8_t   sign;
  uint8_t  degree;
  uint8_t  minute;
  uint32_t frac;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static uint32_t                         posfixflag;
static struct cxd56_gnss_positiondata_s posdat;
static volatile int                     g_gnss_running = 1;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: signal_handler()
 *
 * Description:
 *   Handle signals to stop the daemon.
 ****************************************************************************/

static void signal_handler(int signo)
{
  g_gnss_running = 0;
}

/****************************************************************************
 * Name: show_usage()
 *
 * Description:
 *   Display usage information.
 ****************************************************************************/

static void show_usage(const char *progname)
{
  printf("Usage: %s [command] [options]\n", progname);
  printf("Commands:\n");
  printf("  start     Start continuous GNSS monitoring\n");
  printf("  stop      Stop GNSS monitoring\n");
  printf("  (none)    Read GNSS data once\n");
  printf("Options:\n");
  printf("  -i ID     Specify request ID (for daemon mode)\n");
  printf("  -t MS     Specify polling interval in milliseconds (default: 1000)\n");
  printf("  -h        Show this help message\n");
}

/****************************************************************************
 * Name: double_to_dmf()
 *
 * Description:
 *   Convert from double format to degree-minute-frac format.
 ****************************************************************************/

static void double_to_dmf(double x, struct cxd56_gnss_dms_s *dmf)
{
  int    b;
  int    d;
  int    m;
  double f;
  double t;

  if (x < 0)
    {
      b = 1;
      x = -x;
    }
  else
    {
      b = 0;
    }
  d = (int)x; /* = floor(x), x is always positive */
  t = (x - d) * 60;
  m = (int)t; /* = floor(t), t is always positive */
  f = (t - m) * 10000;

  dmf->sign   = b;
  dmf->degree = d;
  dmf->minute = m;
  dmf->frac   = f;
}

/****************************************************************************
 * Name: read_and_print_json()
 *
 * Description:
 *   Read POS data and print in JSON format.
 ****************************************************************************/

static int read_and_print_json(int fd, int request_id, int count)
{
  int ret;
  struct cxd56_gnss_dms_s dmf_lat, dmf_lng;
  static char output[1024];

  /* Read POS data. */
  ret = read(fd, &posdat, sizeof(posdat));
  if (ret < 0)
    {
      printf("{\"status\":{\"code\":%d,\"msg\":\"Read error\"}}\n", -errno);
      return -1;
    }
  else if (ret != sizeof(posdat))
    {
      printf("{\"status\":{\"code\":-1,\"msg\":\"Read size error\"}}\n");
      return -1;
    }

  /* Format time string */
  char time_str[32];
  snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d.%06ld",
           posdat.receiver.time.hour, posdat.receiver.time.minute,
           posdat.receiver.time.sec, posdat.receiver.time.usec);

  /* Start building JSON output */
  snprintf(output, sizeof(output),
           "{\"data\":{\"time\":\"%s\"",
           time_str);

  if (posdat.receiver.pos_fixmode != CXD56_GNSS_PVT_POSFIX_INVALID)
    {
      /* 2D fix or 3D fix */
      posfixflag = 1;

      /* Convert latitude and longitude to DMF format */
      double_to_dmf(posdat.receiver.latitude, &dmf_lat);
      double_to_dmf(posdat.receiver.longitude, &dmf_lng);

      /* Format latitude and longitude strings */
      char lat_str[32], lng_str[32];
      snprintf(lat_str, sizeof(lat_str), "%d.%d.%04ld",
               dmf_lat.degree, dmf_lat.minute, dmf_lat.frac);
      snprintf(lng_str, sizeof(lng_str), "%d.%d.%04ld",
               dmf_lng.degree, dmf_lng.minute, dmf_lng.frac);

      char pos_data[768];
      snprintf(pos_data, sizeof(pos_data),
               ",\"lat\":\"%s\",\"lng\":\"%s\",\"lat_raw\":%.8f,\"lng_raw\":%.8f,"
               "\"fix\":%d,\"altitude\":%.2f,\"geoid\":%.2f,"
               "\"h_accuracy\":%.2f,\"v_accuracy\":%.2f,"
               "\"visible_sats\":%d,\"tracking_sats\":%d,\"pos_sats\":%d,"
               "\"sat_systems\":%d,"
               "\"pdop\":%.2f,\"hdop\":%.2f,\"vdop\":%.2f,\"tdop\":%.2f},\"status\":{\"code\":0,\"msg\":\"Position fixed\"}}",
               lat_str, lng_str,
               posdat.receiver.latitude, posdat.receiver.longitude,
               posdat.receiver.pos_fixmode,
               posdat.receiver.altitude, posdat.receiver.geoid,
               posdat.receiver.pos_accuracy.hvar, posdat.receiver.pos_accuracy.vvar,
               posdat.receiver.numsv, posdat.receiver.numsv_tracking, posdat.receiver.numsv_calcpos,
               posdat.receiver.svtype,
               posdat.receiver.pos_dop.pdop, posdat.receiver.pos_dop.hdop,
               posdat.receiver.pos_dop.vdop, posdat.receiver.pos_dop.tdop);

      strncat(output, pos_data, sizeof(output) - strlen(output) - 1);
    }
  else
    {
      /* No position fix */
      char no_fix_data[256];
      snprintf(no_fix_data, sizeof(no_fix_data),
               ",\"fix\":0,\"visible_sats\":%d,\"tracking_sats\":%d,"
               "\"sat_systems\":%d},\"status\":{\"code\":0,\"msg\":\"No position fix\"}}",
               posdat.receiver.numsv, posdat.receiver.numsv_tracking,
               posdat.receiver.svtype);

      strncat(output, no_fix_data, sizeof(output) - strlen(output) - 1);
    }

  /* Print the JSON output */
  printf("%s\n", output);

  return 0;
}

/****************************************************************************
 * Name: read_and_print()
 *
 * Description:
 *   Read and print POS data.
 ****************************************************************************/

static int read_and_print(int fd, int fd_tty, int request_id, int interval_ms)
{
  int ret;
  struct cxd56_gnss_dms_s dmf;
  static char output[1024];

  /* Read POS data. */
  ret = read(fd, &posdat, sizeof(posdat));
  if (ret < 0)
    {
      printf("read error\n");
      return -1;
    }
  else if (ret != sizeof(posdat))
    {
      printf("read size error\n");
      return -1;
    }

  /* If fd_tty is valid, also output JSON format to TTY */
  if (fd_tty >= 0)
    {
      snprintf(output, sizeof(output),
               "{\"cmd\":\"gnss\",\"type\":\"poll\",\"interval\":%d,\"id\":%d,"
               "\"data\":{\"time\":\"%d:%d:%d.%ld\"",
               interval_ms, request_id,
               posdat.receiver.time.hour, posdat.receiver.time.minute,
               posdat.receiver.time.sec, posdat.receiver.time.usec);

      if (posdat.receiver.pos_fixmode != CXD56_GNSS_PVT_POSFIX_INVALID)
        {
          /* 2D fix or 3D fix */
          double_to_dmf(posdat.receiver.latitude, &dmf);
          char lat_str[32];
          snprintf(lat_str, sizeof(lat_str), "%d.%d.%04ld",
                   dmf.degree, dmf.minute, dmf.frac);

          double_to_dmf(posdat.receiver.longitude, &dmf);
          char lng_str[32];
          snprintf(lng_str, sizeof(lng_str), "%d.%d.%04ld",
                   dmf.degree, dmf.minute, dmf.frac);

          /* Append position data to output */
          char pos_data[768];
          snprintf(pos_data, sizeof(pos_data),
                   ",\"lat\":\"%s\",\"lng\":\"%s\",\"lat_raw\":%.8f,\"lng_raw\":%.8f,"
                   "\"fix\":%d,\"altitude\":%.2f,\"geoid\":%.2f,"
                   "\"h_accuracy\":%.2f,\"v_accuracy\":%.2f,"
                   "\"visible_sats\":%d,\"tracking_sats\":%d,\"pos_sats\":%d,"
                   "\"sat_systems\":%d,"
                   "\"pdop\":%.2f,\"hdop\":%.2f,\"vdop\":%.2f,\"tdop\":%.2f},\"status\":{\"code\":0,\"msg\":\"Position fixed\"}}",
                   lat_str, lng_str,
                   posdat.receiver.latitude, posdat.receiver.longitude,
                   posdat.receiver.pos_fixmode,
                   posdat.receiver.altitude, posdat.receiver.geoid,
                   posdat.receiver.pos_accuracy.hvar, posdat.receiver.pos_accuracy.vvar,
                   posdat.receiver.numsv, posdat.receiver.numsv_tracking, posdat.receiver.numsv_calcpos,
                   posdat.receiver.svtype,
                   posdat.receiver.pos_dop.pdop, posdat.receiver.pos_dop.hdop,
                   posdat.receiver.pos_dop.vdop, posdat.receiver.pos_dop.tdop);

          strncat(output, pos_data, sizeof(output) - strlen(output) - 1);
        }
      else
        {
          /* No position fix */
          char no_fix_data[128];
          snprintf(no_fix_data, sizeof(no_fix_data),
                   ",\"fix\":0,\"visible_sats\":%d,\"tracking_sats\":%d},"
                   "\"status\":{\"code\":0,\"msg\":\"No position fix\"}}\n",
                   posdat.receiver.numsv,
                   posdat.receiver.numsv_tracking);

          strncat(output, no_fix_data, sizeof(output) - strlen(output) - 1);
        }

      write(fd_tty, output, strlen(output));
    }

  return 0;
}

/****************************************************************************
 * Name: gnss_setparams()
 *
 * Description:
 *   Set gnss parameters use ioctl.
 ****************************************************************************/

static int gnss_setparams(int fd, uint32_t interval_ms, uint32_t satellite_system)
{
  int      ret = 0;
  struct cxd56_gnss_ope_mode_param_s set_opemode;

  /* Set the GNSS operation interval. */

  set_opemode.mode     = 1;            /* Operation mode:Normal(default). */
  set_opemode.cycle    = interval_ms;  /* Position notify cycle(msec step). */

  ret = ioctl(fd, CXD56_GNSS_IOCTL_SET_OPE_MODE, (uint32_t)&set_opemode);
  if (ret < 0)
    {
      printf("ioctl(CXD56_GNSS_IOCTL_SET_OPE_MODE) NG!!\n");
      return ret;
    }

  ret = ioctl(fd, CXD56_GNSS_IOCTL_SELECT_SATELLITE_SYSTEM, satellite_system);
  if (ret < 0)
    {
      printf("ioctl(CXD56_GNSS_IOCTL_SELECT_SATELLITE_SYSTEM) NG!!\n");
      return ret;
    }

  return ret;
}

/****************************************************************************
 * Name: save_daemon_pid()
 *
 * Description:
 *   Save daemon PID to file.
 ****************************************************************************/

static int save_daemon_pid(pid_t pid)
{
  FILE *fp = fopen(GNSS_PID_FILE, "w");
  if (!fp) return -errno;

  fprintf(fp, "%d", pid);
  fclose(fp);

  return 0;
}

/****************************************************************************
 * Name: load_daemon_pid()
 *
 * Description:
 *   Load daemon PID from file.
 ****************************************************************************/

static pid_t load_daemon_pid(void)
{
  FILE *fp = fopen(GNSS_PID_FILE, "r");
  if (!fp) return -1;

  pid_t pid;
  if (fscanf(fp, "%d", &pid) != 1)
    {
      fclose(fp);
      return -1;
    }

  fclose(fp);
  return pid;
}

/****************************************************************************
 * Name: is_daemon_running()
 *
 * Description:
 *   Check if daemon is running.
 ****************************************************************************/

static int is_daemon_running(void)
{
  pid_t pid = load_daemon_pid();
  if (pid <= 0) return 0;

  if (kill(pid, 0) == 0)
    {
      return 1;  /* Process exists */
    }
  return 0;  /* Process does not exist */
}

/****************************************************************************
 * Name: run_gnss_daemon()
 *
 * Description:
 *   Run GNSS daemon to continuously monitor position.
 ****************************************************************************/

static void run_gnss_daemon(int request_id, uint32_t interval_ms, uint32_t satellite_system)
{
  int      fd;
  int      fd_tty;
  int      ret;
  sigset_t mask;
  struct cxd56_gnss_signal_setting_s setting;

  /* Set up signal handler */
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  /* Get file descriptor to control GNSS */
  fd = open(GNSS_DEVICE_PATH, O_RDONLY);
  if (fd < 0)
    {
      printf("open error:%d,%d\n", fd, errno);
      exit(1);
    }

  /* Configure mask to notify GNSS signal */
  sigemptyset(&mask);
  sigaddset(&mask, MY_GNSS_SIG);
  ret = sigprocmask(SIG_BLOCK, &mask, NULL);
  if (ret != OK)
    {
      printf("sigprocmask failed. %d\n", ret);
      close(fd);
      exit(1);
    }

  /* Set the signal to notify GNSS events */
  setting.fd      = fd;
  setting.enable  = 1;
  setting.gnsssig = CXD56_GNSS_SIG_GNSS;
  setting.signo   = MY_GNSS_SIG;
  setting.data    = NULL;

  ret = ioctl(fd, CXD56_GNSS_IOCTL_SIGNAL_SET, (unsigned long)&setting);
  if (ret < 0)
    {
      printf("signal error\n");
      close(fd);
      exit(1);
    }

  /* Set GNSS parameters */
  ret = gnss_setparams(fd, interval_ms, satellite_system);
  if (ret != OK)
    {
      printf("gnss_setparams failed. %d\n", ret);
      setting.enable = 0;
      ioctl(fd, CXD56_GNSS_IOCTL_SIGNAL_SET, (unsigned long)&setting);
      close(fd);
      exit(1);
    }

  /* Open TTY device for output */
  fd_tty = open(TTY_OUTPUT_PATH, O_WRONLY);
  if (fd_tty < 0)
    {
      printf("Failed to open TTY device: %d\n", errno);
      setting.enable = 0;
      ioctl(fd, CXD56_GNSS_IOCTL_SIGNAL_SET, (unsigned long)&setting);
      close(fd);
      exit(1);
    }

  /* Start GNSS */
  ret = ioctl(fd, CXD56_GNSS_IOCTL_START, CXD56_GNSS_STMOD_HOT);
  if (ret < 0)
    {
      printf("start GNSS ERROR %d\n", errno);
      setting.enable = 0;
      ioctl(fd, CXD56_GNSS_IOCTL_SIGNAL_SET, (unsigned long)&setting);
      close(fd);
      close(fd_tty);
      exit(1);
    }

  /* Send initial response to indicate polling has started */
  char init_msg[256];
  snprintf(init_msg, sizeof(init_msg),
           "{\"cmd\":\"gnss\",\"type\":\"res\",\"status\":{\"code\":0,\"msg\":\"GNSS monitoring started\"},\"id\":%d}\n",
           request_id);
  write(fd_tty, init_msg, strlen(init_msg));

  /* Main loop - wait for signals and read data */
  while (g_gnss_running)
    {
      /* Wait for positioning data with timeout */
      ret = sigwaitinfo(&mask, NULL);
      if (ret != MY_GNSS_SIG)
        {
          break;
        }

      /* Read and print POS data */
      ret = read_and_print(fd, fd_tty, request_id, interval_ms);
      if (ret < 0)
        {
          break;
        }
    }

  /* Stop GNSS */
  ret = ioctl(fd, CXD56_GNSS_IOCTL_STOP, 0);
  if (ret < 0)
    {
      printf("stop GNSS ERROR\n");
    }

  /* Disable signal */
  setting.enable = 0;
  ret = ioctl(fd, CXD56_GNSS_IOCTL_SIGNAL_SET, (unsigned long)&setting);
  if (ret < 0)
    {
      printf("signal error\n");
    }

  /* Close file descriptors */
  close(fd_tty);
  close(fd);

  /* Remove PID file */
  unlink(GNSS_PID_FILE);
}

/****************************************************************************
 * Name: main()
 *
 * Description:
 *   Main entry point for the GNSS daemon application.
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  int opt;
  char *cmd = NULL;
  uint32_t interval_ms = 1000;
  uint32_t satellite_system = 3;  // GPS+GLONASS
  int request_id = 0;
  int ret;

  /* Process command argument */
  if (argc > 1 && argv[1][0] != '-')
    {
      cmd = argv[1];
      /* Remove command from argument list */
      for (int i = 1; i < argc - 1; i++)
        {
          argv[i] = argv[i + 1];
        }
      argc--;
    }

  /* Process option arguments */
  optind = 1;
  while ((opt = getopt(argc, argv, "i:t:s:h")) != -1)
    {
      switch (opt)
        {
          case 'i':
            request_id = atoi(optarg);
            break;
          case 't':
            interval_ms = atoi(optarg);
            if (interval_ms < 1000)
              {
                printf("{\"status\":{\"code\":-1,\"msg\":\"Interval must be at least 1000ms\"}}\n");
                return 1;
              }
            break;
          case 's':
            satellite_system = atoi(optarg);
            break;
          case 'h':
            show_usage(argv[0]);
            return 0;
          default:
            show_usage(argv[0]);
            return 1;
        }
    }

  /* Handle "stop" command */
  if (cmd && strcmp(cmd, "stop") == 0)
    {
      /* Check if daemon process is running */
      pid_t daemon_pid = load_daemon_pid();
      if (daemon_pid <= 0)
        {
          printf("{\"status\":{\"code\":-1,\"msg\":\"GNSS daemon not running\"}}\n");
          return 1;
        }

      /* Send SIGTERM to daemon process */
      if (kill(daemon_pid, SIGTERM) < 0)
        {
          ret = -errno;
          printf("{\"status\":{\"code\":%d,\"msg\":\"Failed to stop GNSS daemon\"}}\n", ret);
          return 1;
        }

      /* Wait briefly for daemon to clean up */
      usleep(100000);
      unlink(GNSS_PID_FILE);
      printf("{\"status\":{\"code\":0,\"msg\":\"GNSS monitoring stopped successfully\"}}\n");
      return 0;
    }
  /* Handle "start" command */
  else if (cmd && strcmp(cmd, "start") == 0)
    {
      /* Check if daemon is already running */
      if (is_daemon_running())
        {
          pid_t old_pid = load_daemon_pid();
          kill(old_pid, SIGTERM);
          unlink(GNSS_PID_FILE);
          usleep(500000);
        }

      /* Fork a new process for the daemon */
      pid_t pid = fork();
      if (pid < 0)
        {
          /* Fork failed */
          ret = -errno;
          printf("{\"status\":{\"code\":%d,\"msg\":\"Failed to create daemon process\"}}\n", ret);
          return 1;
        }
      else if (pid == 0)
        {
          /* Child process (daemon) */

          /* Close standard input */
          close(STDIN_FILENO);
          /* Don't close STDOUT/STDERR to allow error reporting */

          /* Save daemon PID */
          save_daemon_pid(getpid());

          /* Run GNSS daemon */
          g_gnss_running = 1;
          run_gnss_daemon(request_id, interval_ms, satellite_system);
          exit(0);
        }
      else
        {
          /* Parent process */

          /* Save daemon PID */
          save_daemon_pid(pid);
          printf("{\"status\":{\"code\":0,\"msg\":\"GNSS monitoring started successfully\"}}\n");
          return 0;
        }
    }
  /* Handle single read (default mode) */
  else
    {
      int fd;
      sigset_t mask;
      struct cxd56_gnss_signal_setting_s setting;
      int count = 0;
      int max_count = 10;

      /* Get file descriptor to control GNSS. */
      fd = open(GNSS_DEVICE_PATH, O_RDONLY);
      if (fd < 0)
        {
          printf("{\"status\":{\"code\":%d,\"msg\":\"Failed to open GNSS device\"}}\n", -errno);
          return -ENODEV;
        }

      /* Configure mask to notify GNSS signal. */
      sigemptyset(&mask);
      sigaddset(&mask, MY_GNSS_SIG);
      ret = sigprocmask(SIG_BLOCK, &mask, NULL);
      if (ret != OK)
        {
          printf("{\"status\":{\"code\":%d,\"msg\":\"sigprocmask failed\"}}\n", ret);
          goto _err;
        }

      /* Set the signal to notify GNSS events. */
      setting.fd      = fd;
      setting.enable  = 1;
      setting.gnsssig = CXD56_GNSS_SIG_GNSS;
      setting.signo   = MY_GNSS_SIG;
      setting.data    = NULL;

      ret = ioctl(fd, CXD56_GNSS_IOCTL_SIGNAL_SET, (unsigned long)&setting);
      if (ret < 0)
        {
          printf("{\"status\":{\"code\":%d,\"msg\":\"Signal setting failed\"}}\n", -errno);
          goto _err;
        }

      /* Set GNSS parameters. */
      ret = gnss_setparams(fd, interval_ms, satellite_system);
      if (ret != OK)
        {
          printf("{\"status\":{\"code\":%d,\"msg\":\"gnss_setparams failed\"}}\n", ret);
          goto _err;
        }

      /* Start GNSS. */
      ret = ioctl(fd, CXD56_GNSS_IOCTL_START, CXD56_GNSS_STMOD_HOT);
      if (ret < 0)
        {
          printf("{\"status\":{\"code\":%d,\"msg\":\"Failed to start GNSS\"}}\n", -errno);
          goto _err;
        }
      else
        {
          printf("{\"status\":{\"code\":0,\"msg\":\"GNSS started successfully\"}}\n");
        }

      /* Loop to get 10 position readings */
      while (count < max_count)
        {
          /* Wait for positioning data */
          ret = sigwaitinfo(&mask, NULL);
          if (ret != MY_GNSS_SIG)
            {
              printf("{\"status\":{\"code\":%d,\"msg\":\"sigwaitinfo error\"}}\n", ret);
              break;
            }

          /* Read and print POS data in JSON format */
          ret = read_and_print_json(fd, request_id, count);
          if (ret < 0)
            {
              break;
            }

          count++;
        }

    _err:
      /* Stop GNSS. */
      ret = ioctl(fd, CXD56_GNSS_IOCTL_STOP, 0);
      if (ret < 0)
        {
          printf("{\"status\":{\"code\":%d,\"msg\":\"Failed to stop GNSS\"}}\n", -errno);
        }

      /* GNSS firmware needs to disable the signal after positioning. */
      setting.enable = 0;
      ret = ioctl(fd, CXD56_GNSS_IOCTL_SIGNAL_SET, (unsigned long)&setting);
      if (ret < 0)
        {
          printf("{\"status\":{\"code\":%d,\"msg\":\"Failed to disable signal\"}}\n", -errno);
        }

      sigprocmask(SIG_UNBLOCK, &mask, NULL);

      /* Release GNSS file descriptor. */
      ret = close(fd);
      if (ret < 0)
        {
          printf("{\"status\":{\"code\":%d,\"msg\":\"Failed to close device\"}}\n", -errno);
        }

      return ret;
    }
}

