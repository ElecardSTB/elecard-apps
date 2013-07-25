#
#                             IPSTB Project
#                   ---------------------------------
#
# Copyright (C) 2005 Koninklijke Philips Electronics N.V.,
# All Rights Reserved.
#
# Filename: StbDvbRecord/makefile
#
#
# Rev Date       Author      Comments
#-------------------------------------------------------------------------------
#   1 20050713   steel       Original
#   2 20050923   burningh    Installing as Watch, Record and Play
#   3 20060801   laird       Using build system.
#   4 20060802   laird       Xcheck rework.
#   5 20061103   laird       Moving to NXP

PROGRAM = StbPvr

#LOCAL_CFLAGS = -DNO_FRONTEND

ifeq ($(ARCH),mips)
include $(PRJROOT)/etc/application_first.mk
endif

C_SOURCES = $(wildcard src/*.c)

LOCAL_CFLAGS = -Wall -Wextra \
               -I$(STAGINGDIR)/opt/elecard/include -Iinclude \
               -I$(PRJROOT)/src/project/elecard/apps/StbMainApp/include \
               -I$(STAGINGDIR)/usr/include -I$(PRJROOT)/src/apps/StbMainApp/include \

LOCAL_LDFLAGS = -L$(ROOTFS)/opt/elecard/lib -L$(ROOTFS)/usr/lib -lnet -lcommon -lpthread -lcurl

INSTALL_DIR = $(ROOTFS)/opt/elecard/bin

ifeq ($(ARCH),mips)
LOCAL_CFLAGS += -DCONFIG_DIR="\"/config/StbMainApp\""
include $(PRJROOT)/etc/application_last.mk
else
ifneq ($(STAPISDK_VERSION),)
LOCAL_CFLAGS += -DCONFIG_DIR="\"/var/etc/elecard/StbMainApp\""
endif

HEADERS = $(wildcard include/*.h)

all: $(PROGRAM)

$(PROGRAM): $(C_SOURCES) $(HEADERS)
	$(CC) $(LOCAL_CFLAGS) $(CFLAGS) $(LOCAL_LDFLAGS) $(LDFLAGS) -o $@ $(C_SOURCES)

install: $(PROGRAM)
	cp include/$(PROGRAM).h $(STAGINGDIR)/opt/elecard/include/
	install -m0755 $(PROGRAM) $(ROOTFS)/opt/elecard/bin/

clean:
	rm -f $(PROGRAM)
endif
