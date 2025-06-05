include $(APPDIR)/Make.defs
include $(SDKDIR)/Make.defs

PROGNAME = $(CONFIG_EXAMPLES_SPRINFO_PROGNAME)
PRIORITY = $(CONFIG_EXAMPLES_SPRINFO_PRIORITY)
STACKSIZE = $(CONFIG_EXAMPLES_SPRINFO_STACKSIZE)
MODULE = $(CONFIG_EXAMPLES_SPRINFO)

ASRCS =
CSRCS =
MAINSRC = sprinfo_main.c

VERSION_JSON := $(TOPDIR)/../firmware/spresense/version.json
VERSION_HEADER := $(TOPDIR)/../sdk/include/spresense_version.h

.PHONY: generate_version_header
generate_version_header:
	@echo "Generating $(VERSION_HEADER) from $(VERSION_JSON)"
	@python $(CURDIR)/generate_version.py $(VERSION_JSON) $(VERSION_HEADER)

$(OBJS): generate_version_header
all:: generate_version_header

include $(APPDIR)/Application.mk
