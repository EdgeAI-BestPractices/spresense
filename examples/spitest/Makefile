
include $(APPDIR)/Make.defs
include $(SDKDIR)/Make.defs

PROGNAME = $(CONFIG_EXAMPLES_SPITEST_PROGNAME)
PRIORITY = $(CONFIG_EXAMPLES_SPITEST_PRIORITY)
STACKSIZE = $(CONFIG_EXAMPLES_SPITEST_STACKSIZE)
MODULE = $(CONFIG_EXAMPLES_SPITEST)

ASRCS =
CSRCS =
MAINSRC = spitest_main.c

CFLAGS += -I $(SDKDIR)/../nuttx/arch/arm/src/cxd56xx
CXXFLAGS += -I $(SDKDIR)/../nuttx/arch/arm/src/cxd56xx

include $(APPDIR)/Application.mk
