#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <sys/ioctl.h>

#include <nuttx/sensors/cxd5602pwbimu.h>

#include "imu_utils/imu_utils.h"

#define CXD5602PWBIMU_DEVPATH "/dev/imu0"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/** start_imusensing()
 * @rate    [in]: 15, 30, 60, 120, 240, 480, 960, 1920
 * @adrange [in]: 2, 4, 8, 16
 * @gdrange [in]: 125, 250, 500, 1000, 2000, 4000
 * @nfifos  [in]: 1, 2, 3, 4
 */
int pwbimu_start_sensing(int rate, int adrange, int gdrange, int nfifos) {
  cxd5602pwbimu_range_t range;
  int ret;
  int fd;

  fd = open(CXD5602PWBIMU_DEVPATH, O_RDONLY);
  if (fd < 0) {
    printf("{\"status\":{\"code\":%d,\"msg\":\"Failed to open sensor device\"}}\n", -errno);
    return -errno;
  }

  ret = ioctl(fd, SNIOC_SSAMPRATE, rate);
  if (ret) {
    int current_errno = errno;
    printf("{\"status\":{\"code\":%d,\"msg\":\"Failed to set sampling rate\"}}\n", ret);
    close(fd);
    return -current_errno;
  }

  range.accel = adrange;
  range.gyro = gdrange;
  ret = ioctl(fd, SNIOC_SDRANGE, (unsigned long)(uintptr_t)&range);
  if (ret) {
    int current_errno = errno;
    printf("{\"status\":{\"code\":%d,\"msg\":\"Failed to set dynamic range\"}}\n", ret);
    close(fd);
    return -current_errno;
  }

  ret = ioctl(fd, SNIOC_SFIFOTHRESH, nfifos);
  if (ret) {
    int current_errno = errno;
    printf("{\"status\":{\"code\":%d,\"msg\":\"Failed to set FIFO number\"}}\n", ret);
    close(fd);
    return -current_errno;
  }

  ret = ioctl(fd, SNIOC_ENABLE, 1);
  if (ret) {
    int current_errno = errno;
    printf("{\"status\":{\"code\":%d,\"msg\":\"Failed to enable sensor\"}}\n", ret);
    close(fd);
    return -current_errno;
  }

  return fd;
}

int pwbimu_read_imudata(int fd, int nfifos, cxd5602pwbimu_data_t *imudata) {
  int ret;
  struct pollfd fds[1];
  int keep_trying = 1;

  fds[0].fd = fd;
  fds[0].events = POLLIN;

  while (keep_trying) {
    ret = poll(fds, 1, -1);
    if (ret <= 0) {
        printf("{\"status\":{\"code\":%d,\"msg\":\"Poll failed\"}}\n", -errno);
      keep_trying = 0;
      ret = 0;
    } else {
      if (fds[0].revents & POLLIN) {
        ret = read(fd, imudata, sizeof(*imudata) * nfifos);
        if (ret == sizeof(*imudata) * nfifos) {
          keep_trying = 0;
        }
      }
    }
  }

  return ret;
}

int pwbimu_drop_data(int fd, int millis, int samprate, int nfifos, cxd5602pwbimu_data_t *imudata) {
  int cnt = samprate * millis / 1000;

  if (cnt == 0)
    cnt = 1;

  while (cnt) {
    pwbimu_read_imudata(fd, nfifos, imudata);
    cnt--;
  }

  return 0;
}

int pwbimu_terminate(int fd) {
  int ret = ioctl(fd, SNIOC_ENABLE, 0);
  if (ret) {
    return ret;
  }
  return close(fd);
}
