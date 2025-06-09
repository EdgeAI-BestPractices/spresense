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

ifeq ($(CONFIG_EXAMPLES_SPRINFO),y)
$(VERSION_HEADER): $(VERSION_JSON)
	@echo "Generating $@ from $<"
	@python3 $(CURDIR)/generate_version.py $< $@

$(MAINSRC): $(VERSION_HEADER)
endif

include $(APPDIR)/Application.mk
