#ifndef IMU_UTILS_H
#define IMU_UTILS_H

#include <nuttx/sensors/cxd5602pwbimu.h>

int pwbimu_start_sensing(int rate, int adrange, int gdrange, int nfifos);
int pwbimu_read_imudata(int fd, int nfifos, cxd5602pwbimu_data_t *imudata);
int pwbimu_drop_data(int fd, int samprate, int millis, int nfifos, cxd5602pwbimu_data_t *imudata);
int pwbimu_terminate(int fd);

#endif // IMU_UTILS_H
