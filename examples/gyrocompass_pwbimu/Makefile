
include $(APPDIR)/Make.defs
include $(SDKDIR)/Make.defs

PROGNAME = $(CONFIG_EXAMPLES_GYROCOMPASS_PWBIMU_PROGNAME)
PRIORITY = $(CONFIG_EXAMPLES_GYROCOMPASS_PWBIMU_PRIORITY)
STACKSIZE = $(CONFIG_EXAMPLES_GYROCOMPASS_PWBIMU_STACKSIZE)
MODULE = $(CONFIG_EXAMPLES_GYROCOMPASS_PWBIMU)

ASRCS =
CSRCS = imu_utils.c gyrocompass.c
MAINSRC = gyrocompass_pwbimu_main.c

ifneq ($(CONFIG_EXAMPLES_GYROCOMPASS_TEST),)
CSRCS += test_on_pc/gyrocompass_test.c
endif

include $(APPDIR)/Application.mk
