
#
# Elecard STB820 Demo Application
# Copyright (C) 2007  Elecard Devices
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 1, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
#

# -----------------------------------------------------------------------------
# Common variables
# -----------------------------------------------------------------------------
# use the next variables:
#
# PRJROOT - project top directory
# STAGINGDIR - dirty target fs with usefull scripts, headers, libraries and other. Uses for compiling (headers search directories) and linking (library search directories).
# ROOTFSDIR - directory with rootfs, commonly uses for installing.
# ROOTFSNFS_DIR - auxiliary rootfs uses for sharing via nfs.
STAGINGDIR ?= $(STAGING_DIR)
ROOTFS ?= $(TARGET_DIR)

# Force use bash
SHELL := /bin/bash

# Platform overrides
# -----------------------------------------------------------------------------
ifeq ($(ARCH),mips)
ifeq ($(_TMBSL),_stb225)     # --- STB225 ---
STAGINGDIR = $(BUILDROOT)/staging

ENABLE_LAN := 0
ENABLE_XWORKS := 0
ENABLE_DLNA := 0
else                         # --- STB820 ---
ENABLE_LAN ?= 1
ENABLE_XWORKS ?= 1
ENABLE_DLNA ?= 1
ENABLE_PVR ?= 1
endif

ENABLE_BROWSER ?= 1
ENABLE_USE_CJSON := 1
#KDIR ?= $(KSRC)
endif #ifeq ($(ARCH),mips)

ifneq ($(STAPISDK_VERSION),) # --- STB83x/STB84x ---
ARCH := sh4

ENABLE_LAN ?= 0
ENABLE_WIFI ?= 1
ENABLE_PPP ?= 1
ENABLE_XWORKS ?= 0
ENABLE_DLNA ?= 1
ENABLE_HDDESIGN ?= 0
ENABLE_USE_CJSON := 1
ENABLE_ANALOGTV := 1
ENABLE_USE_LIBDVBAPI := 1

ROOTFSNFS_DIR := $(ROOTFS)_nfs
else # $(ARCH) != sh4
ENABLE_HDDESIGN := 0
endif # $(ARCH) != sh4

ifneq ($(UEMD),)             # --- Module ---
ENABLE_GSTREAMER ?= 1
ENABLE_DLNA ?= 1
ROOTFSNFS_DIR := $(BUILDROOT)/rootfs_nfs
endif
ifeq ($(ARCH),)              # --- PC x86/x64 ---
ARCH := x86
ENABLE_GSTREAMER ?= 1
endif

# Default settings
# -----------------------------------------------------------------------------
TEST_MODE ?= 0
ENABLE_DVB ?= 1
ENABLE_IPTV ?= 1
ENABLE_VOD ?= 1
ENABLE_USB ?= 1
ENABLE_FAVORITES ?= 1
ENABLE_WEB_SERVICES ?= 1
ENABLE_RUTUBE ?= 1
ENABLE_VERIMATRIX ?= 0
ENABLE_SECUREMEDIA ?= 0
ENABLE_PVR ?= 0
ENABLE_BROWSER ?= 0
ENABLE_XWORKS ?= 0
ENABLE_DLNA ?= 0
ENABLE_VIDIMAX ?= 0
ENABLE_EXPAT ?= 0
ENABLE_PPP ?= 0
ENABLE_WIFI ?= 0
ENABLE_MOBILE ?= 0
ENABLE_SCARDS ?= 0
ENABLE_REGPLAT ?= 0
PROVIDER_PROFILES ?= 0
ENABLE_PASSWORD ?= 1
ENABLE_MESSAGES ?= 0
#TELETES a.k.a. OFTMedia
ENABLE_TELETES ?= 0
ENABLE_3DRENDERING ?= 0
ENABLE_GSTREAMER ?= 0
ENABLE_USE_CJSON ?= 0
ENABLE_TELETEXT ?= 0
ENABLE_ANALOGTV ?= 0
ENABLE_USE_LIBDVBAPI ?= 0

TEST_SERVER ?= 1

INTERFACE_SPLASH_IMAGE ?= "splash.jpg"

# Deprecated
INCLUDE_CCRTP ?= 0
FORCE_HELPER_SURFACE ?= 0

# -----------------------------------------------------------------------------
# Common build rules
# -----------------------------------------------------------------------------

REVISION ?= $(shell git rev-parse HEAD 2>/dev/null | cut -c1-7)
COMPILE_TIME ?= $(shell date)
RELEASE_TYPE ?= Custom

#separate CFLAGS for StbMainApp.c
SEP_FLAGS_StbMainApp.c = \
	-DRELEASE_TYPE="\"$(RELEASE_TYPE)\"" \

#separate CFLAGS for output.c
SEP_FLAGS_output.c = \
	-DREVISION="\"$(REVISION)\"" \
	-DCOMPILE_TIME="\"$(COMPILE_TIME)\"" \
	$(SEP_FLAGS_StbMainApp.c)


LOCAL_CFLAGS += \
	-std=gnu99 \
	-DINTERFACE_SPLASH_IMAGE="\"$(INTERFACE_SPLASH_IMAGE)\"" \
	-Wall -Wextra -Winline -Wno-unused-parameter \
	-Iinclude \
	-Isrc \
	-I$(STAGINGDIR)/opt/elecard/include \
	-D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE \
	-finline-functions \
	-g \

LOCAL_CXXFLAGS += \
	-O3 -w \
	-Itinyxml \

LOCAL_LDFLAGS += \
	-lnet \
	-lcommon \
	-Xlinker -rpath -Xlinker /opt/elecard/lib \
	-L$(STAGINGDIR)/opt/elecard/lib \
	-L$(ROOTFS)/opt/elecard/lib \
	-lrt -lpthread -Xlinker -z -Xlinker muldefs


C_SOURCES = $(wildcard src/*.c)

CXX_SOURCES = \
	$(wildcard src/*.cpp) \
	$(wildcard tinyxml/*.cpp) \
	TSDemuxGetStreams/TSGetStreamInfo.cpp \

INSTALL_EXTRA = install_common
ifeq ($(ARCH),mips)
PROGRAM := StbMainApp
LOCAL_PREFIXES := /usr
#H_FILES += $(wildcard include/*.h)
#LOCAL_CFLAGS += $(SEP_FLAGS_output.c)

include $(PRJROOT)/etc/application_first.mk

else #$(ARCH) != mips
CFLAGS = $(LOCAL_CFLAGS)
CXXFLAGS = $(LOCAL_CXXFLAGS) $(LOCAL_CFLAGS)
LDFLAGS = $(LOCAL_LDFLAGS)

#default target
all:
endif #$(ARCH) != mips

# -----------------------------------------------------------------------------
# Variable flags
# -----------------------------------------------------------------------------

ifeq ($(INCLUDE_CCRTP),1)
    LOCAL_LDFLAGS += \
	-lccrtp1 -lccgnu2 -lccext2 -lcrypto -lssl

    LOCAL_CFLAGS += -DINCLUDE_CCRTP
endif

ifneq ($(BUILD_LANG),)
    LOCAL_CFLAGS += -DLANG=$(BUILD_LANG)
endif

ifeq ($(TEST_MODE),1)
    LOCAL_CFLAGS += -DENABLE_TEST_MODE=$(TEST_MODE)
endif

ifeq ($(TEST_SERVER),1)
    LOCAL_CFLAGS += -DENABLE_TEST_SERVER=$(TEST_SERVER)
endif

ifeq ($(ENABLE_DLNA),1)
    LOCAL_LDFLAGS += -LDLNALib/$(ARCH) -ledlna
    LOCAL_CFLAGS += -IDLNALib \
		-IDLNALib/CdsObjects \
		-IDLNALib/HttpFiles \
		-IDLNALib/MediaServerBrowser \
		-IDLNALib/FileAbstractionFiles \
		-IDLNALib/ProtocolInfoParser \
		-IDLNALib/StringUtils \
		-IDLNALib/PlaylistTrackManager \
		-IDLNALib/MediaRenderer \
		-IDLNALib/PlaySingleUri \
		-DENABLE_DLNA=$(ENABLE_DLNA)

    INSTALL_EXTRA += install_libdlna
    DEPENDS_EXTRA += libdlna
endif

ifeq ($(ENABLE_VERIMATRIX),1)
    LOCAL_LDFLAGS += -lvmclient -lclientid -lvmerror
    LOCAL_CFLAGS += -DENABLE_VERIMATRIX=$(ENABLE_VERIMATRIX) -I$(STAGINGDIR)/usr/include/verimatrix
endif

ifeq ($(ENABLE_SECUREMEDIA),1)
    SM=$(PRJROOT)/src/project/elecard/apps/SecureMedia
    LOCAL_LDFLAGS += '-Wl,--start-group' $(SM)/libs/ctoolkit.a $(SM)/libs/smclient.a $(SM)/libs/smelecard.a $(SM)/libs/smes.a $(SM)/libs/smlinux.a $(SM)/libs/smplatform.a -lpthread -lm '-Wl,--end-group'
    LOCAL_CFLAGS += -DENABLE_SECUREMEDIA=$(ENABLE_SECUREMEDIA) -I$(SM)/includes
endif

ifeq ($(ENABLE_EXPAT),1)
    LOCAL_CFLAGS += -DENABLE_EXPAT=$(ENABLE_EXPAT)
    LOCAL_LDFLAGS += -lexpat
endif
ifeq ($(ENABLE_EXPAT),2)
    # Dynamic linking
    LOCAL_CFLAGS += -DENABLE_EXPAT=$(ENABLE_EXPAT)
    LOCAL_LDFLAGS += -ldl
endif

ifeq ($(ENABLE_DVB),1)
    LOCAL_CFLAGS += -DENABLE_DVB=$(ENABLE_DVB)
endif

ifeq ($(ENABLE_IPTV),1)
    LOCAL_CFLAGS += -DENABLE_IPTV=$(ENABLE_IPTV)
endif

ifeq ($(ENABLE_VOD),1)
	LOCAL_CFLAGS += -DENABLE_VOD=$(ENABLE_VOD)
endif

ifeq ($(ENABLE_USB),1)
    LOCAL_CFLAGS += -DENABLE_USB=$(ENABLE_USB)
endif

ifeq ($(ENABLE_FAVORITES),1)
    LOCAL_CFLAGS += -DENABLE_FAVORITES=$(ENABLE_FAVORITES)
endif

ifeq ($(ENABLE_WEB_SERVICES),1)
    LOCAL_CFLAGS += -DENABLE_WEB_SERVICES=$(ENABLE_WEB_SERVICES)
endif

ifeq ($(ENABLE_RUTUBE),1)
    LOCAL_CFLAGS += -DENABLE_RUTUBE=$(ENABLE_RUTUBE)
endif

ifeq ($(ENABLE_LAN),1)
	LOCAL_CFLAGS += -DENABLE_LAN=$(ENABLE_LAN)
endif

ifeq ($(ENABLE_PPP),1)
	LOCAL_CFLAGS += -DENABLE_PPP=$(ENABLE_PPP)
endif

ifeq ($(ENABLE_WIFI),1)
	LOCAL_CFLAGS += -DENABLE_WIFI=$(ENABLE_WIFI)
	LOCAL_LDFLAGS += -liw
endif

ifeq ($(ENABLE_MOBILE),1)
	LOCAL_CFLAGS += -DENABLE_MOBILE=$(ENABLE_MOBILE)
	LOCAL_LDFLAGS += -liw
endif

ifeq ($(ENABLE_PVR),1)
	LOCAL_CFLAGS += -DENABLE_PVR=$(ENABLE_PVR)
endif

ifeq ($(ENABLE_GSTREAMER),1)
	GST_VERSION       := $(shell if pkg-config --exists gstreamer-0.10; then echo "0.10"; elif pkg-config --exists gstreamer-1.0; then echo "1.0"; fi)
	GST_LOCAL_CFLAGS  := $(shell pkg-config --cflags gstreamer-$(GST_VERSION)) -DENABLE_GSTREAMER=$(ENABLE_GSTREAMER)
	GST_LOCAL_LDFLAGS := $(shell pkg-config --libs   gstreamer-$(GST_VERSION))
	LOCAL_CFLAGS  += $(GST_LOCAL_CFLAGS)
	LOCAL_LDFLAGS += $(GST_LOCAL_LDFLAGS)
endif

ifeq ($(ENABLE_HDDESIGN),1)
	LOCAL_CFLAGS += -DENABLE_HDDESIGN=$(ENABLE_HDDESIGN)
	INSTALL_EXTRA += install_hdfiles
	HD_SHARE_SUBDIR := opt/elecard/share/hd
endif

ifeq ($(ENABLE_3DRENDERING),1)
	LOCAL_CFLAGS += -DENABLE_3D=$(ENABLE_3DRENDERING)
endif

ifeq ($(ENABLE_BROWSER),1)
	LOCAL_CFLAGS += -DENABLE_BROWSER=$(ENABLE_BROWSER)
endif

ifeq ($(ENABLE_XWORKS),1)
	LOCAL_CFLAGS += -DENABLE_XWORKS=$(ENABLE_XWORKS)
endif

ifeq ($(PROVIDER_PROFILES),1)
	LOCAL_CFLAGS += -DENABLE_PROVIDER_PROFILES=$(PROVIDER_PROFILES)
endif

ifeq ($(ENABLE_PASSWORD),1)
	LOCAL_CFLAGS += -DENABLE_PASSWORD=$(ENABLE_PASSWORD)
endif

ifeq ($(ENABLE_MESSAGES),1)
	LOCAL_CFLAGS += -DENABLE_MESSAGES=$(ENABLE_MESSAGES)
endif
ifeq ($(ENABLE_MESSAGES),2)
	LOCAL_CFLAGS += -DENABLE_MESSAGES=$(ENABLE_MESSAGES) -DMESSAGES_NAGGING
endif

ifneq ($(RTP_RECONNECT),)
	LOCAL_CFLAGS += -DRTP_RECONNECT=$(RTP_RECONNECT)
endif

ifneq ($(DEFAULT_FONT),)
	LOCAL_CFLAGS += -DDEFAULT_FONT=\"$(DEFAULT_FONT)\"
endif

ifneq ($(INTERFACE_WALLPAPER_IMAGE),)
	LOCAL_CFLAGS += -DINTERFACE_WALLPAPER_IMAGE=\"$(INTERFACE_WALLPAPER_IMAGE)\"
endif

ifeq ($(ENABLE_TVMYWAY),1)
	LOCAL_CFLAGS += -DENABLE_TVMYWAY=$(ENABLE_TVMYWAY)
endif

ifeq ($(ENABLE_USE_CJSON),1)
	LOCAL_CFLAGS += -I$(PRJROOT)/src/project/elecard/apps/cJSON/include
	LOCAL_CFLAGS += -I$(PRJROOT)/src/apps/cJSON/include
	LOCAL_LDFLAGS += -lcjson
endif

ifeq ($(ENABLE_TELETEXT),1)
	LOCAL_CFLAGS += -DENABLE_TELETEXT
endif

ifeq ($(ENABLE_FUSION),1)
	LOCAL_CFLAGS += -DENABLE_FUSION=$(ENABLE_FUSION)
endif

ifeq ($(ENABLE_ANALOGTV),1)
	LOCAL_CFLAGS += -DENABLE_ANALOGTV
endif

ifeq ($(ENABLE_USE_LIBDVBAPI),1)
	LOCAL_LDFLAGS += -ldvbapi -lucsi
endif

# -----------------------------------------------------------------------------
# File layout
# -----------------------------------------------------------------------------

TARGET_DATA_DIR=/opt/elecard/share/StbMainApp
INSTALL_BIN_DIR = $(ROOTFS)/opt/elecard/bin
LIB_DIR =     $(ROOTFS)/opt/elecard/lib
DATA_DIR =    $(ROOTFS)$(TARGET_DATA_DIR)

ifeq ($(ARCH),sh4)
DEFAULTS_DIR =  $(ROOTFS)/etc/defaults/elecard/StbMainApp
SYSTEM_CONFIG_DIR := /var/etc
TARGET_CONFIG_DIR := $(SYSTEM_CONFIG_DIR)/elecard/StbMainApp

LINUXTV_VERSION := $(shell grep -E "^LINUXTV_VERSION :=" $(BUILDROOT)/packages/buildroot/package/elecard/linuxtv/linuxtv.mk | cut -d ' ' -f 3)
LOCAL_CFLAGS += -I$(BUILDROOT)/packages/buildroot/output_rootfs/build/linuxtv-$(LINUXTV_VERSION)/linux/include/uapi

endif #$(ARCH) == sh4

ifeq ($(ARCH),mips)
DEFAULTS_DIR = $(ROOTFS)/config.defaults/StbMainApp
SYSTEM_CONFIG_DIR := /config
TARGET_CONFIG_DIR := $(SYSTEM_CONFIG_DIR)/StbMainApp
endif

DEFAULTS_DIR ?= $(ROOTFS)/opt/elecard/etc/defaults
# NB: TARGET_CONFIG_DIR is evaluated on target!
TARGET_CONFIG_DIR ?= /opt/elecard/etc
SYSTEM_CONFIG_DIR ?= /etc

LOCAL_CFLAGS += \
	-DIMAGE_DIR="\"$(TARGET_DATA_DIR)/images/\"" \
	-DFONT_DIR="\"$(TARGET_DATA_DIR)/fonts/\"" \
	-DCONFIG_DIR="\"$(TARGET_CONFIG_DIR)\"" \
	-DSYSTEM_CONFIG_DIR="\"$(SYSTEM_CONFIG_DIR)\"" \
	-DRTSP_STREAM_FILE="\"$(TARGET_CONFIG_DIR)/streams.txt\"" \
	-DCHANNEL_FILE_NAME="\"$(TARGET_CONFIG_DIR)/channels.conf\"" \
	-DSETTINGS_FILE="\"$(TARGET_CONFIG_DIR)/settings.conf\"" \
	-DPLAYLIST_FILENAME="\"$(TARGET_CONFIG_DIR)/playlist.txt\"" \

# -----------------------------------------------------------------------------
# Platform dependant flags
# -----------------------------------------------------------------------------

ifeq ($(ARCH),x86) # PC x86/x64
# -----------------------------------------------------------------------------
LOCAL_CXXFLAGS += -fpermissive

LIBCURL_CFLAGS ?= $(shell pkg-config --cflags libcurl)
LIBCURL_LDFLAGS ?= $(shell pkg-config --libs libcurl)
DIRECTFB_CFLAGS ?= $(shell pkg-config --cflags directfb-internal)
DIRECTFB_LDFLAGS ?= $(shell pkg-config --libs directfb-internal)

LOCAL_CFLAGS += \
	$(LIBCURL_CFLAGS) \
	$(DIRECTFB_CFLAGS) \

LOCAL_LDFLAGS += \
	$(LIBCURL_LDFLAGS) \
	$(DIRECTFB_LDFLAGS) \
	-lm

else #$(ARCH) != x86
# STB
# -----------------------------------------------------------------------------
ifeq ($(FORCE_HELPER_SURFACE),1)
LOCAL_CFLAGS += \
	-DGFX_USE_HELPER_SURFACE
endif

LOCAL_CFLAGS += \
	-I$(KDIR)/include \
	-I$(STAGINGDIR)/usr/include \
	-I$(STAGINGDIR)/usr/include/directfb \
	-I$(STAGINGDIR)/usr/local/include/directfb \
	-I$(STAGINGDIR)/usr/include/directfb-internal \

LOCAL_LDFLAGS += \
	-ldirectfb$(DFB_VERSION) \
	-ldirect$(DFB_VERSION) \
	-lfusion$(DFB_VERSION) \
	-lcurl -lssl -lcrypto \
	-L$(STAGINGDIR)/usr/lib \
	-L$(STAGINGDIR)/usr/local/lib \
	-L$(ROOTFS)/usr/lib \

endif #$(ARCH) != x86

ifeq ($(ARCH),mips)
# STB820
# -----------------------------------------------------------------------------

LOCAL_CFLAGS += \
	-I$(_phStbAnalogBackend_DIR)/inc \
	-I$(_phStbGpio_DIR)/inc \
	-I$(_phStbIAmAlive_DIR)/inc \
	-I$(_phStbDFBVideoProviderCommon_DIR)/inc \
	-I$(_phStbSystemManager_DIR)/inc \
	-I$(_phStbDFB_DIR)/inc \
	-I$(BUILDROOT) \
	-I$(PRJROOT)/src/project/elecard/apps/StbPvr/include \
	-I/opt/philips/Tools/SDK/overlay/dfb/include/glib-2.0 \
	-I/opt/philips/Tools/SDK/overlay/dfb/include/glib-2.0/glib \
	-I/opt/philips/Tools/SDK/overlay/dfb/lib/glib-2.0/include \

LOCAL_LDFLAGS += -lstdc++ -lasound

ifeq ($(_TMBSL),_stb225)
# STB225
# -----------------------------------------------------------------------------
LOCAL_CFLAGS += \
	-DSTBxx \
	-DSTBPNX \
	-DSTB225 \
	-I$(_tmSbm_DIR)/inc \
	-I$(_tmgdrTypes_DIR)/inc \
	-I$(_tmGsl_DIR)/inc \
	-I$(_tmosa2osal_DIR)/inc \
	-I$(_tmosal_DIR)/inc \
	-I$(_phStbSystemManager_DIR)/inc \
	-I$(_phStbDFBVideoProviderCommonElc_DIR)/inc \
	-I$(_phStbDFBVideoProvider225Elc_DIR)/inc \
	-I$(_IphStbCommon_DIR)/inc \
	-I$(_phStbRpc_DIR)/inc \
	-I$(_phStbRCProtocol_DIR)/inc \
	-I$(_phStbFB_DIR)/inc \
	-Wno-strict-aliasing \

REQUIRES = \
	phStbDFBVideoProvider225Elc \

LIBS =	phStbSbmSink \
	phKernelIfce \
	phStbDbg \
	phStbSystemManager

LOCAL_CXXFLAGS += $(LOCAL_CFLAGS)

else #$(_TMBSL) != _stb225

LOCAL_CFLAGS += \
	-DSTBxx \
	-DSTBPNX \
	-DSTB6x8x \
	-DSTB82 \

CFLAGS += \
	-Wno-strict-prototypes \

LIBS += phStbEvent \
	phStbMpegTsTrickMode	\
	phStbSystemManager
endif #$(_TMBSL) != _stb225

endif #$(ARCH) == mips

ifeq ($(ARCH),arm)
ifneq ($(UEMD),)
# Module
# -----------------------------------------------------------------------------
LOCAL_CFLAGS += \
	-DSTBuemd \
	--sysroot=$(STAGINGDIR) \
	-DGST_VIDEO_SINK=\"mvdusink\" \
	-DGST_AUDIO_SINK=\"alsasink\" \

LOCAL_LDFLAGS += \
	--sysroot=$(STAGINGDIR) \

INSTALL_EXTRA += install_nfs
endif

ifneq ($(STBTI),)
COMPS_DIR=../../comps

LOCAL_CFLAGS += -DNull=NULL \
	-DSTB6x8x \
	-DSTBxx \
	-DSTBTI \
	-I$(COMPS_DIR)/phStbDFBVideoProviderCommon/inc
endif

endif # $(ARCH) == arm

ifeq ($(ARCH),sh4)
# STB83x / STB84x
# -----------------------------------------------------------------------------
-include $(BUILDROOT)/.prjconfig

LOCAL_CFLAGS += \
		--sysroot=$(STAGINGDIR) \
		-DSTSDK=1 \
		-I$(PRJROOT)/src/apps/elcdRpcLib/include \
		-I$(STAGINGDIR)/usr/include/glib-2.0 \
		-I$(STAGINGDIR)/usr/lib/glib-2.0/include

LOCAL_LDFLAGS += \
		--sysroot=$(STAGINGDIR) \
		-lelcdrpc

DFB_VERSION := -1.4
CROSS_COMPILE := sh4-linux-

#CONFIG_UNTAR_ROOTFS_FOR_NFS - defined in $(BUILDROOT)/.prjconfig
ifdef CONFIG_UNTAR_ROOTFS_FOR_NFS
INSTALL_EXTRA += install_nfs
endif
endif #$(ARCH) == sh4

# -----------------------------------------------------------------------------
# Common
# -----------------------------------------------------------------------------
CC  = $(CROSS_COMPILE)gcc
CXX = $(CROSS_COMPILE)g++
AR  = $(CROSS_COMPILE)ar
LD  = $(CROSS_COMPILE)g++

OBJ_DIR := output/$(ARCH)

PHONY += all build install clean install_nfs install_common force
V ?= 0
ifeq ($(V),1)
  quiet=
  Q = 
else
  quiet=quiet_
  Q = @
endif

squote := '
escsq = $(subst $(squote),'\$(squote)',$1)
get_cmd_file = $(dir $1).$(notdir $1).cmd
get_cmd_file_4target = $(call get_cmd_file,$@)

any-prereq = $(filter-out $(PHONY),$?) $(filter-out $(PHONY) $(wildcard $^),$^)
arg-check = $(strip $(filter-out $(cmd_$(1)), $(cmd_$@)) \
                    $(filter-out $(cmd_$@),   $(cmd_$(1))) )

echo-cmd = $(if $($(quiet)cmd_$(1)),\
	echo '  $(call escsq,$($(quiet)cmd_$(1)))$(echo-why)';)

make-cmd = $(subst \#,\\\#,$(subst $$,$$$$,$(call escsq,$(cmd_$(1)))))

if_changed = $(if $(strip $(any-prereq) $(arg-check)),					\
	@set -e;									\
	rm -f $(get_cmd_file_4target);							\
	$(echo-cmd) $(cmd_$(1));							\
	if [ -e "$(get_cmd_file_4target)" ]; then					\
		sed -i "s%.*:%deps_$@ :=%" $(get_cmd_file_4target);			\
		echo -e "\n$@: \$$(deps_$@)\n\$$(deps_$@):" >>$(get_cmd_file_4target);	\
	fi;										\
	echo -e '\n\ncmd_$@ := $(make-cmd)' >> $(get_cmd_file_4target);)

#	echo "$(any-prereq) $(arg-check)"; \


GEN_DEP_FILE_FLAGS = -MD -MF $(get_cmd_file_4target)

quiet_cmd_cc_o_c = CC  $@
#cmd_cc_o_c = $(CC) $(GEN_DEP_FILE_FLAGS) $(LOCAL_CFLAGS) $(LOCAL_CFLAGS_$(notdir $<)) -c $< -o $@
cmd_cc_o_c = $(CC) $(GEN_DEP_FILE_FLAGS) $(CFLAGS) $(SEP_FLAGS_$(notdir $<)) -c $< -o $@
quiet_cmd_cxx_o_cpp = CPP $@
#cmd_cxx_o_cpp = $(CXX) $(GEN_DEP_FILE_FLAGS) $(LOCAL_CXXFLAGS) $(LOCAL_CXXFLAGS_$(notdir $<)) -c $< -o $@
cmd_cxx_o_cpp = $(CXX) $(GEN_DEP_FILE_FLAGS) $(CXXFLAGS) $(SEP_FLAGS_$(notdir $<)) -c $< -o $@
quiet_cmd_ld_out_o = LD  $@
#cmd_ld_out_o = $(LD) -o $@ $(OBJECTS) $(ADD_LIBS) $(LOCAL_LDFLAGS)
cmd_ld_out_o = $(LD) -o $@ $(OBJECTS) $(ADD_LIBS) $(LDFLAGS)
quiet_cmd_archive_o = AR  $@
cmd_archive_o = $(AR) rcs $@ $(filter-out $(PHONY) $(DIRECTORIES),$^)


$(OBJ_DIR)/%.o: %.c force
	$(call if_changed,cc_o_c)

$(OBJ_DIR)/%.o: %.cpp force
	$(call if_changed,cxx_o_cpp)


LIBDLNA := DLNALib/$(ARCH)/libedlna.so
$(LIBDLNA):
	make CROSS_COMPILE=$(CROSS_COMPILE) BUILD_TARGET=$(ARCH)/ -C DLNALib all

PHONY += libdlna
libdlna: $(LIBDLNA)

install_libdlna: $(LIB_DIR) $(LIBDLNA)
	$(Q)install -m0755 $(LIBDLNA) $(LIB_DIR)


# -----------------------------------------------------------------------------
# Third party targets
# -----------------------------------------------------------------------------
ifeq ($(ENABLE_SCARDS),1)
SCARDS_SUBDIR := third_party/smartcards
SCARDS_LIB = $(OBJ_DIR)/$(SCARDS_SUBDIR)/libcard.a
ADD_LIBS += $(SCARDS_LIB)
LOCAL_LDFLAGS += -lpcsclite
LOCAL_CFLAGS += -DENABLE_SCARDS=1 -I$(STAGINGDIR)/usr/include/PCSC

SCARDS_OBJECTS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(wildcard $(SCARDS_SUBDIR)/*.c))
SCARDS_OBJECTS += $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(wildcard $(SCARDS_SUBDIR)/*.cpp))
SCARDS_DIRS += $(sort $(dir $(SCARDS_OBJECTS) $(SCARDS_LIB)))
DIRECTORIES += $(SCARDS_DIRS)
cmd_files += $(wildcard $(foreach f,$(SCARDS_LIB) $(SCARDS_OBJECTS),$(call get_cmd_file,$(f))))

$(SCARDS_OBJECTS): | $(SCARDS_DIRS)

$(SCARDS_LIB): $(SCARDS_OBJECTS) force
	$(call if_changed,archive_o)

endif

ifeq ($(ENABLE_REGPLAT),1)
REGPLAT_SUBDIR := third_party/regplat
REGPLAT_LIB = $(OBJ_DIR)/$(REGPLAT_SUBDIR)/libregplat.a
ADD_LIBS += $(REGPLAT_LIB)
LOCAL_CFLAGS += -DENABLE_REGPLAT=1
#LOCAL_LDFLAGS += -lregplat -ldl
INSTALL_EXTRA += install_regplat

REGPLAT_OBJECTS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(wildcard $(REGPLAT_SUBDIR)/*.c))
REGPLAT_OBJECTS += $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(wildcard $(REGPLAT_SUBDIR)/*.cpp))
REGPLAT_DIRS += $(sort $(dir $(REGPLAT_OBJECTS) $(REGPLAT_LIB)))
DIRECTORIES += $(REGPLAT_DIRS)
cmd_files += $(wildcard $(foreach f,$(REGPLAT_LIB) $(REGPLAT_OBJECTS),$(call get_cmd_file,$(f))))

$(REGPLAT_OBJECTS): | $(REGPLAT_DIRS)

$(REGPLAT_LIB): $(REGPLAT_OBJECTS) force
	$(call if_changed,archive_o)
endif


ifeq ($(ENABLE_TELETES),1)
TELETES_SUBDIR := third_party/teletes
TELETES_LIB = $(OBJ_DIR)/$(TELETES_SUBDIR)/libteletes.a
ADD_LIBS += $(TELETES_LIB)
LOCAL_CFLAGS += -DENABLE_TELETES=1
INSTALL_EXTRA += install_teletes

TELETES_OBJECTS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(wildcard $(TELETES_SUBDIR)/*.c))
TELETES_OBJECTS += $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(wildcard $(TELETES_SUBDIR)/*.cpp))
TELETES_DIRS += $(sort $(dir $(TELETES_OBJECTS) $(TELETES_LIB)))
DIRECTORIES += $(TELETES_DIRS)
cmd_files += $(wildcard $(foreach f,$(TELETES_LIB) $(TELETES_OBJECTS),$(call get_cmd_file,$(f))))

$(TELETES_OBJECTS): | $(TELETES_DIRS)

$(TELETES_LIB): $(TELETES_OBJECTS) force
	$(call if_changed,archive_o)
endif


ifeq ($(ENABLE_VIDIMAX),1)

VM_SUBDIR_SHARE := opt/elecard/share/vidimax
VIDIMAX_SUBDIR := third_party/vidimax
VIDIMAX_LIB = $(OBJ_DIR)/$(VIDIMAX_SUBDIR)/libvidimax.a
ADD_LIBS += $(VIDIMAX_LIB)
LOCAL_CFLAGS += -DENABLE_VIDIMAX=1
ifeq ($(INTERFACE_WALLPAPER_IMAGE),)
LOCAL_CFLAGS += -DINTERFACE_WALLPAPER_IMAGE=\"/usr/local/share/vidimax/images/main_background.png\"
endif
SEP_FLAGS_vidimax.c := $(SEP_FLAGS_output.c)
INSTALL_EXTRA += install_vidimax

VIDIMAX_OBJECTS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(wildcard $(VIDIMAX_SUBDIR)/*.c))
#VIDIMAX_OBJECTS += $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(wildcard $(VIDIMAX_SUBDIR)/*.cpp))
VIDIMAX_DIRS += $(sort $(dir $(VIDIMAX_OBJECTS) $(VIDIMAX_LIB)))
DIRECTORIES += $(VIDIMAX_DIRS)
cmd_files += $(wildcard $(foreach f,$(VIDIMAX_LIB) $(VIDIMAX_OBJECTS),$(call get_cmd_file,$(f))))

$(VIDIMAX_OBJECTS): | $(VIDIMAX_DIRS)

$(VIDIMAX_LIB): $(VIDIMAX_OBJECTS) force
	$(call if_changed,archive_o)
endif

# -----------------------------------------------------------------------------
# Build targets
# -----------------------------------------------------------------------------
#ifeq ($(ARCH),mips)
#OBJECTS += $(ADD_LIBS)

#include $(PRJROOT)/etc/application_last.mk
#else
all: build install

PROG_TARGET = $(OBJ_DIR)/StbMainApp

OBJECTS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(C_SOURCES))
OBJECTS += $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(CXX_SOURCES))
OBJECTS_DIRS := $(sort $(OBJ_DIR) $(dir $(OBJECTS)))
DIRECTORIES += $(OBJECTS_DIRS)

LOCAL_CXXFLAGS += $(LOCAL_CFLAGS)

cmd_files += $(wildcard $(foreach f,$(OBJECTS) $(PROG_TARGET),$(call get_cmd_file,$(f))))
#$(info "cmd_files=$(cmd_files)")
ifneq ($(cmd_files),)
  include $(cmd_files)
endif

$(OBJECTS): | $(OBJECTS_DIRS)

build: $(OBJECTS_DIRS) $(PROG_TARGET)

$(PROG_TARGET): $(OBJECTS) $(ADD_LIBS) $(DEPENDS_EXTRA) force
	$(call if_changed,ld_out_o)

install: build $(DATA_DIR) $(INSTALL_BIN_DIR) $(INSTALL_EXTRA)
	@echo 'INSTALL $(PROG_TARGET) -> $(INSTALL_BIN_DIR)'
	$(Q)install -m0755 $(PROG_TARGET) $(INSTALL_BIN_DIR)

clean:
	rm -f $(ADD_LIBS) $(OBJECTS) $(cmd_files) $(PROG_TARGET)

#endif # $(ARCH) != mips

install_hdfiles:
	rm -rf $(ROOTFS)/$(HD_SHARE_SUBDIR)/*
	$(Q)mkdir -p $(ROOTFS)/$(HD_SHARE_SUBDIR)
	$(Q)mkdir -p $(ROOTFS)/$(HD_SHARE_SUBDIR)/fonts
	#$(Q)mkdir -p $(ROOTFS)/$(HD_SHARE_SUBDIR)/icons
	$(Q)mkdir -p $(ROOTFS)/$(HD_SHARE_SUBDIR)/images
	#$(Q)install -m0644 hd/design/icons/*.png $(ROOTFS)/$(HD_SHARE_SUBDIR)/icons/
	$(Q)install -m0644 hd/design/images/*.png $(ROOTFS)/$(HD_SHARE_SUBDIR)/images/
	#$(Q)install -m0644 hd/design/images/*.jpg $(ROOTFS)/$(HD_SHARE_SUBDIR)/images/
	$(Q)install -m0644 hd/design/fonts/*.ttf $(ROOTFS)/$(HD_SHARE_SUBDIR)/fonts/
	# font license information
	$(Q)install -m0644 hd/design/fonts/*.txt $(ROOTFS)/$(HD_SHARE_SUBDIR)/fonts/

install_regplat:
	@echo 'INSTALL $(REGPLAT_SUBDIR)/resources/images -> $(DATA_DIR)/images'
	-$(Q)install -m0644 $(REGPLAT_SUBDIR)/resources/images/*.png $(DATA_DIR)/images/
	-$(Q)install -m0644 $(REGPLAT_SUBDIR)/resources/images/*.jpg $(DATA_DIR)/images/

install_teletes:
	@echo 'INSTALL third_party/teletes/resources/images -> $(DATA_DIR)/images'
	-$(Q)install -m0644 third_party/teletes/resources/images/*.png $(DATA_DIR)/images/
	-$(Q)install -m0644 third_party/teletes/resources/images/*.jpg $(DATA_DIR)/images/

#TODO: is we should depend on $(ARCH) ???
ifeq ($(ARCH),sh4)
VM_RES_PREFIX := third_party/vidimax/resources
endif
ifeq ($(ARCH),x86)
VM_RES_PREFIX := hd
endif

install_vidimax :
ifeq ($(filter $(ARCH),sh4 x86),$(ARCH)) # ($(ARCH) == sh4) || ($(ARCH) == x86)
	rm -rf $(ROOTFS)/$(VM_SUBDIR_SHARE)/*
	$(Q)mkdir -p $(ROOTFS)/$(VM_SUBDIR_SHARE)
	$(Q)mkdir -p $(ROOTFS)/$(VM_SUBDIR_SHARE)/fonts
	$(Q)mkdir -p $(ROOTFS)/$(VM_SUBDIR_SHARE)/icons
	$(Q)mkdir -p $(ROOTFS)/$(VM_SUBDIR_SHARE)/images
	$(Q)install -m0644 $(VM_RES_PREFIX)/*.json $(ROOTFS)/$(VM_SUBDIR_SHARE)/
	$(Q)install -m0644 $(VM_RES_PREFIX)/fonts/*.ttf $(ROOTFS)/$(VM_SUBDIR_SHARE)/fonts/
	$(Q)install -m0644 $(VM_RES_PREFIX)/icons/*.png $(ROOTFS)/$(VM_SUBDIR_SHARE)/icons/
	$(Q)install -m0644 $(VM_RES_PREFIX)/images/*.png $(ROOTFS)/$(VM_SUBDIR_SHARE)/images/
	$(Q)install -m0644 $(VM_RES_PREFIX)/images/*.jpg $(ROOTFS)/$(VM_SUBDIR_SHARE)/images/
endif


install_nfs: build
ifneq ($(ROOTFSNFS_DIR),)
	@echo 'INSTALL to NFS directory: $(ROOTFSNFS_DIR)'
	$(Q)mkdir -p $(ROOTFSNFS_DIR)/opt/elecard/lib $(ROOTFSNFS_DIR)/opt/elecard/bin $(ROOTFSNFS_DIR)/opt/elecard/share
	$(Q)install -m0755 $(PROG_TARGET) $(ROOTFSNFS_DIR)/opt/elecard/bin
	$(Q)rm -rf $(ROOTFSNFS_DIR)/opt/elecard/share/StbMainApp
	$(Q)cp -r $(DATA_DIR) $(ROOTFSNFS_DIR)/opt/elecard/share/
ifeq ($(ENABLE_VIDIMAX),1)
	$(Q)rm -rf $(ROOTFSNFS_DIR)/$(VM_SUBDIR_SHARE)
	$(Q)cp -r $(ROOTFS)/$(VM_SUBDIR_SHARE) $(ROOTFSNFS_DIR)/$(VM_SUBDIR_SHARE)
endif
ifeq ($(ENABLE_HDDESIGN),1)
	$(Q)rm -rf $(ROOTFSNFS_DIR)/$(HD_SHARE_SUBDIR)
	$(Q)cp -r $(ROOTFS)/$(HD_SHARE_SUBDIR) $(ROOTFSNFS_DIR)/$(HD_SHARE_SUBDIR)
endif
endif

install_common: $(DATA_DIR) $(DEFAULTS_DIR)
	@echo '  INSTALL'
	$(Q)mkdir -p $(DATA_DIR)/images $(DATA_DIR)/fonts $(DATA_DIR)/languages $(DATA_DIR)/sounds
	$(Q)rm -f $(DATA_DIR)/images/* $(DATA_DIR)/fonts/* $(DATA_DIR)/languages/* $(DATA_DIR)/sounds/*
	@echo 'INSTALL images -> $(DATA_DIR)/images'
	$(Q)install -m0644 images/*.png $(DATA_DIR)/images
	$(Q)install -m0644 images/*.jpg $(DATA_DIR)/images || true
# 	@echo 'INSTALL streams.txt & channels.conf & settings.conf -> $(DEFAULTS_DIR)'
# 	$(Q)install -m0644 streams.txt $(DEFAULTS_DIR)
# 	$(Q)install -m0644 channels.conf $(DEFAULTS_DIR)
# 	$(Q)install -m0644 settings.conf $(DEFAULTS_DIR)
	@echo 'INSTALL fonts -> $(DATA_DIR)/fonts'
	$(Q)install -m0644 fonts/*.ttf $(DATA_DIR)/fonts
	@echo 'INSTALL languages -> $(DATA_DIR)/languages'
	$(Q)install -m0644 languages/*.lng $(DATA_DIR)/languages
	@echo 'INSTALL sounds -> $(DATA_DIR)/sounds'
	$(Q)install -m0644 sounds/*.wav $(DATA_DIR)/sounds || true

DIRECTORIES += $(DATA_DIR) $(DEFAULTS_DIR) $(INSTALL_BIN_DIR) $(LIB_DIR)
$(DIRECTORIES):
	$(Q)mkdir -p $@

force:

.PHONY: $(PHONY)

