
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

PROGRAM = StbMainApp

# -----------------------------------------------------------------------------
# Common variables
# -----------------------------------------------------------------------------

# Platform overrides
ifeq ($(ARCH),mips)
ifeq ($(_TMBSL),_stb225)
STAGINGDIR = $(BUILDROOT)/staging

ENABLE_LAN := 0
ENABLE_XWORKS := 0
ENABLE_DLNA := 0
else
# STB820
ENABLE_LAN ?= 1
ENABLE_XWORKS ?= 1
ENABLE_DLNA ?= 1
ENABLE_PVR ?= 1
endif

ENABLE_BROWSER ?= 1
endif

ifneq ($(STAPISDK_VERSION),)
ARCH= sh4

ENABLE_LAN ?= 0
ENABLE_WIFI ?= 1
ENABLE_PPP ?= 1
ENABLE_XWORKS := 0
ENABLE_DLNA ?= 1
ENABLE_HDDESIGN ?= 0
else
ENABLE_HDDESIGN := 0
endif

# Default settings
TEST_MODE ?= 0
ENABLE_VOD ?= 1
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
ENABLE_SCARDS ?= 0
ENABLE_REGPLAT ?= 0
PROVIDER_PROFILES ?= 0
ENABLE_PASSWORD ?= 1
ENABLE_MESSAGES ?= 0
#TELETES a.k.a. OFTMedia
ENABLE_TELETES ?= 0

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

LOCAL_CFLAGS = \
	-DINTERFACE_SPLASH_IMAGE="\"$(INTERFACE_SPLASH_IMAGE)\"" \
	-DREVISION="\"$(REVISION)\"" \
	-DRELEASE_TYPE="\"$(RELEASE_TYPE)\"" \
	-DCOMPILE_TIME="\"$(COMPILE_TIME)\"" \
	-Wall -Wextra -Winline -Wno-unused-parameter \
	-Iinclude \
	-I$(STAGINGDIR)/opt/elecard/include \
	-D_FILE_OFFSET_BITS=64 \
	-finline-functions \
	-g \

LOCAL_CXXFLAGS = \
	-O3 -w \
	-Itinyxml \

LOCAL_LDFLAGS = \
	-lnet \
	-lcommon \
	-Xlinker -rpath -Xlinker /opt/elecard/lib \
	-L$(ROOTFS)/opt/elecard/lib \
	-lrt -lpthread -Xlinker -z -Xlinker muldefs

H_FILES += $(wildcard include/*.h)

C_SOURCES = $(wildcard src/*.c)

CXX_SOURCES = \
	$(wildcard src/*.cpp) \
	$(wildcard tinyxml/*.cpp) \
	TSDemuxGetStreams/TSGetStreamInfo.cpp \

ifeq ($(ARCH),mips)
include $(PRJROOT)/etc/application_first.mk

LOCAL_PREFIXES = /usr

else

OBJECTS  = $(patsubst %.c,%.o,$(C_SOURCES))
OBJECTS += $(patsubst %.cpp,%.o,$(CXX_SOURCES))

%.o:%.c $(H_FILES)
	$(CC) -c $(LOCAL_CFLAGS) -o $@ $<

%.o:%.cpp $(H_FILES)
	$(CXX) -c $(LOCAL_CXXFLAGS) -o $@ $<
endif

INSTALL_EXTRA = copy_images copy_fonts copy_config copy_laguages copy_sounds

# -----------------------------------------------------------------------------
# Variable flags
# -----------------------------------------------------------------------------

ifeq ($(INCLUDE_CCRTP),1)
LOCAL_LDFLAGS += \
	-lccrtp1 -lccgnu2 -lccext2 -lcrypto -lssl

LOCAL_CFLAGS += \
	-DINCLUDE_CCRTP
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

ifeq ($(ENABLE_SCARDS),1)
    OBJECTS += src/libcard.a
    LOCAL_LDFLAGS += -lpcsclite
    LOCAL_CFLAGS += -DENABLE_SCARDS=$(ENABLE_SCARDS)
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

    LIBDLNA = DLNALib/$(ARCH)/libedlna.so
    INSTALL_EXTRA += copy_libdlna
    DEPENDS_EXTRA += $(LIBDLNA)
endif

ifeq ($(ENABLE_VERIMATRIX),1)
    LOCAL_LDFLAGS += -lvmclient -lclientid -lvmerror
    LOCAL_CFLAGS += -DENABLE_VERIMATRIX=$(ENABLE_VERIMATRIX)
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

ifeq ($(ENABLE_VOD),1)
	LOCAL_CFLAGS += -DENABLE_VOD=$(ENABLE_VOD)
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

ifeq ($(ENABLE_PVR),1)
	LOCAL_CFLAGS += -DENABLE_PVR=$(ENABLE_PVR)
endif

ifeq ($(ENABLE_REGPLAT),1)
	OBJECTS += src/libregplat.a
	LOCAL_CFLAGS += -DENABLE_REGPLAT=$(ENABLE_REGPLAT)
	LOCAL_LDFLAGS += -Lsrc -lregplat -ldl
	INSTALL_EXTRA += copy_regplat
endif

ifeq ($(ENABLE_TELETES),1)
	OBJECTS += src/libteletes.a
	LOCAL_CFLAGS += -DENABLE_TELETES=$(ENABLE_TELETES)
	INSTALL_EXTRA += copy_teletes
endif

ifeq ($(ENABLE_VIDIMAX),1)
	OBJECTS += src/libvidimax.a
	LOCAL_CFLAGS += -DENABLE_VIDIMAX=$(ENABLE_VIDIMAX)
ifeq ($(INTERFACE_WALLPAPER_IMAGE),)
        LOCAL_CFLAGS += -DINTERFACE_WALLPAPER_IMAGE=\"/usr/local/share/vidimax/images/main_background.png\"
endif
	LOCAL_LDFLAGS += -Lsrc -lvidimax
	INSTALL_EXTRA += copy_vidimax
endif

ifeq ($(ENABLE_HDDESIGN),1)
	LOCAL_CFLAGS += -DENABLE_HDDESIGN=$(ENABLE_HDDESIGN)
	INSTALL_EXTRA += copy_hdfiles
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

# -----------------------------------------------------------------------------
# File layout
# -----------------------------------------------------------------------------

TARGET_DATA_DIR=/opt/elecard/share/StbMainApp
INSTALL_DIR = $(ROOTFS)/opt/elecard/bin
LIB_DIR =     $(ROOTFS)/opt/elecard/lib
DATA_DIR =    $(ROOTFS)/$(TARGET_DATA_DIR)

ifneq ($(STAPISDK_VERSION),)
DEFAULTS_DIR =  $(ROOTFS)/etc/defaults/elecard/StbMainApp
TARGET_CONFIG_DIR = /var/etc/elecard/StbMainApp
SYSTEM_CONFIG_DIR = /var/etc
ROOTFS_NFS = $(ROOTFS)_nfs
ifeq ($(ENABLE_VIDIMAX),1)
VM_SHARE = $(ROOTFS)/opt/elecard/share/vidimax
VM_NFS_SHARE = $(ROOTFS_NFS)/opt/elecard/share
endif
ifeq ($(ENABLE_HDDESIGN),1)
HD_SHARE = $(ROOTFS)/opt/elecard/share/hd
HD_NFS_SHARE = $(ROOTFS_NFS)/opt/elecard/share
endif
# STAPISDK
endif

ifeq ($(ARCH),mips)
DEFAULTS_DIR = $(ROOTFS)/config.defaults/StbMainApp
TARGET_CONFIG_DIR = /config/StbMainApp
SYSTEM_CONFIG_DIR = /config
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

ifeq ($(ARCH),)

# Host

LOCAL_CXXFLAGS += -fpermissive

LIBCURL_CFLAGS ?= $(shell pkg-config --cflags libcurl)
LIBCURL_LDFLAGS ?= $(shell pkg-config --libs libcurl)
DIRECTFB_CFLAGS ?= $(shell pkg-config --cflags directfb-internal)
DIRECTFB_LDFLAGS ?= $(shell pkg-config --libs directfb-internal)
GLIB_CFLAGS ?= $(shell pkg-config --cflags glib-2.0)
# GLIB_LDFLAGS ?= $(shell pkg-config --libs glib-2.0)

LOCAL_CFLAGS += \
	$(LIBCURL_CFLAGS) \
	$(DIRECTFB_CFLAGS) \
	$(GLIB_CFLAGS) \

LOCAL_LDFLAGS += \
	$(LIBCURL_LDFLAGS) \
	$(DIRECTFB_LDFLAGS) \
	$(GLIB_LDFLAGS) \
	-lm

ENABLE_VIDIMAX ?= 1

ifeq ($(ENABLE_VIDIMAX),1)
VM_SHARE = $(ROOTFS)/opt/elecard/share/vidimax
VM_NFS_SHARE = $(ROOTFS_NFS)/opt/elecard/share
endif

else

# Target
ifeq ($(FORCE_HELPER_SURFACE),1)
LOCAL_CFLAGS += \
	-DGFX_USE_HELPER_SURFACE
endif

LOCAL_CFLAGS += \
	-I$(STAGINGDIR)/usr/include \
	-I$(STAGINGDIR)/usr/include/PCSC \
	-I$(STAGINGDIR)/usr/include/verimatrix \
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

ifeq ($(ARCH),mips)

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

LIBS += phStbEvent

ifeq ($(_TMBSL),_stb225)

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

else

LOCAL_CFLAGS += \
	-DSTBxx \
	-DSTBPNX \
	-DSTB6x8x \
	-DSTB82 \
	-Wno-strict-prototypes \

LIBS += phStbMpegTsTrickMode	\
	phStbSystemManager
# _TMBSL != _stb225
endif
# ARCH == mips
endif

ifeq ($(ARCH),arm)
ifneq ($(UEMD),)
LOCAL_CFLAGS += -DSTBuemd

CC  = $(CROSS_COMPILE)gcc
CXX = $(CROSS_COMPILE)g++
AR  = $(CROSS_COMPILE)ar
LOCAL_LDFLAGS += --sysroot=$(STAGINGDIR)
endif

ifneq ($(STBTI),)
COMPS_DIR=../../comps

LOCAL_CFLAGS += -DNull=NULL \
	-DSTB6x8x \
	-DSTBxx \
	-DSTBTI \
	-I$(COMPS_DIR)/phStbDFBVideoProviderCommon/inc
endif
# ARCH == arm
endif

ifneq ($(STAPISDK_VERSION),)
-include $(BUILDROOT)/.prjconfig

LOCAL_CFLAGS  += \
	-DSTSDK=1 \
	-Isrc \
	-I$(DVD_ROOT)/st2fx/src/DirectFB-1.4.12/source/src \
	-I$(PRJROOT)/src/apps/elcdRpcLib/include \
	-I$(STAGINGDIR)/usr/include/glib-2.0 -I$(STAGINGDIR)/usr/lib/glib-2.0/include

LOCAL_LDFLAGS += --sysroot=$(STAGINGDIR) -lelcdrpc
DFB_VERSION = -1.4

CROSS_COMPILE=sh4-linux-
CC  = $(CROSS_COMPILE)gcc
CXX = $(CROSS_COMPILE)g++
AR  = $(CROSS_COMPILE)ar

ifdef CONFIG_UNTAR_ROOTFS_FOR_NFS
INSTALL_EXTRA += copy_nfs
endif
# ARCH == sh4
endif

# ARCH != i386
endif

# -----------------------------------------------------------------------------
# Build targets
# -----------------------------------------------------------------------------

ifeq ($(ARCH),mips)
include $(PRJROOT)/etc/application_last.mk
else

H_FILES += $(wildcard src/*.h)

LOCAL_CXXFLAGS += $(LOCAL_CFLAGS)

all: $(OBJECTS) $(PROGRAM) install

$(PROGRAM): $(OBJECTS) $(DEPENDS_EXTRA)
	$(CXX) -o $(PROGRAM) $(OBJECTS) $(LOCAL_LDFLAGS)

install: $(PROGRAM) $(INSTALL_EXTRA)
	$(Q)mkdir -p $(INSTALL_DIR) $(DATA_DIR) $(DEFAULTS_DIR)
	$(Q)install -m0755 $(PROGRAM) $(INSTALL_DIR)

clean:
	rm -f $(OBJECTS) StbMainApp

# ARCH != mips
endif

ifeq ($(ENABLE_DLNA),1)
copy_libdlna: $(LIBDLNA)
	$(Q)mkdir -p $(LIB_DIR)
	$(Q)install -m0755 $(LIBDLNA) $(LIB_DIR)
endif

ifeq ($(ENABLE_SCARDS),1)
src/libcard.a: third_party/smartcards/$(ARCH)/libcard.a
	cp third_party/smartcards/$(ARCH)/libcard.a src/libcard.a

SCARDS_HEADERS = $(wildcard third_party/smartcards/*.h)
SCARDS_SOURCES = $(wildcard third_party/smartcards/*.c)
SCARDS_OBJECTS = $(patsubst %.c,%.o,$(SCARDS_SOURCES))

third_party/smartcards/$(ARCH)/libcard.a: $(SCARDS_HEADERS) $(SCARDS_OBJECTS)
	$(Q)mkdir -p third_party/smartcards/$(ARCH)
	$(AR) rcs third_party/smartcards/$(ARCH)/libcard.a $(SCARDS_OBJECTS)
endif

ifeq ($(ENABLE_REGPLAT),1)
src/libregplat.a: third_party/regplat/$(ARCH)/libregplat.a
	cp third_party/regplat/$(ARCH)/libregplat.a src/libregplat.a

REGPLAT_SOURCES=$(wildcard third_party/regplat/*.c)
REGPLAT_CPPSRCS=$(wildcard third_party/regplat/*.cpp)
REGPLAT_OBJECTS=$(patsubst %.c,%.o,$(REGPLAT_SOURCES)) $(patsubst %.cpp,%.o,$(REGPLAT_CPPSRCS))

third_party/regplat/$(ARCH)/libregplat.a: $(REGPLAT_OBJECTS) $(wildcard third_party/regplat/*.h)
	$(Q)mkdir -p third_party/regplat/$(ARCH)
	$(AR) rcs third_party/regplat/$(ARCH)/libregplat.a $(REGPLAT_OBJECTS)

copy_regplat:
	@echo 'INSTALL third_party/regplat/resources/images -> $(DATA_DIR)/images'
	-$(Q)install -m0644 third_party/regplat/resources/images/*.png $(DATA_DIR)/images/
	-$(Q)install -m0644 third_party/regplat/resources/images/*.jpg $(DATA_DIR)/images/
endif


ifeq ($(ENABLE_TELETES),1)
src/libteletes.a: third_party/teletes/$(ARCH)/libteletes.a
	cp third_party/teletes/$(ARCH)/libteletes.a src/libteletes.a

TELETES_SOURCES=$(wildcard third_party/teletes/*.c)
TELETES_OBJECTS=$(patsubst %.c,%.o,$(TELETES_SOURCES)) $(patsubst %.cpp,%.o,$(TELETES_CPPSRCS))

third_party/teletes/$(ARCH)/libteletes.a: $(TELETES_OBJECTS) $(wildcard third_party/teletes/*.h)
	$(Q)mkdir -p third_party/teletes/$(ARCH)
	$(AR) rcs third_party/teletes/$(ARCH)/libteletes.a $(TELETES_OBJECTS)
copy_teletes:
	@echo 'INSTALL third_party/teletes/resources/images -> $(DATA_DIR)/images'
	-$(Q)install -m0644 third_party/teletes/resources/images/*.png $(DATA_DIR)/images/
	-$(Q)install -m0644 third_party/teletes/resources/images/*.jpg $(DATA_DIR)/images/
endif


ifeq ($(ENABLE_VIDIMAX),1)
src/libvidimax.a: third_party/vidimax/$(ARCH)/libvidimax.a
	cp third_party/vidimax/$(ARCH)/libvidimax.a src/libvidimax.a

VIDIMAX_SOURCES=$(wildcard third_party/vidimax/*.c)
VIDIMAX_OBJECTS=$(patsubst %.c,%.o,$(VIDIMAX_SOURCES))
third_party/vidimax/$(ARCH)/libvidimax.a: $(VIDIMAX_OBJECTS) $(wildcard third_party/vidimax/*.h)
	$(Q)mkdir -p third_party/vidimax/$(ARCH)
	$(AR) rcs third_party/vidimax/$(ARCH)/libvidimax.a $(VIDIMAX_OBJECTS)
endif

copy_hdfiles:
ifneq ($(STAPISDK_VERSION),)
	rm -rf $(HD_SHARE)/*
	$(Q)mkdir -p $(HD_SHARE)
	$(Q)mkdir -p $(HD_SHARE)/fonts
	#$(Q)mkdir -p $(HD_SHARE)/icons
	$(Q)mkdir -p $(HD_SHARE)/images
	#$(Q)install -m0644 hd/design/icons/*.png $(HD_SHARE)/icons/
	$(Q)install -m0644 hd/design/images/*.png $(HD_SHARE)/images/
	#$(Q)install -m0644 hd/design/images/*.jpg $(HD_SHARE)/images/
	$(Q)install -m0644 hd/design/fonts/*.ttf $(HD_SHARE)/fonts/
	# font license information
	$(Q)install -m0644 hd/design/fonts/*.txt $(HD_SHARE)/fonts/
endif

copy_vidimax :
ifneq ($(STAPISDK_VERSION),)
	rm -rf $(VM_SHARE)/*
	$(Q)mkdir -p $(VM_SHARE)
	$(Q)mkdir -p $(VM_SHARE)/fonts
	$(Q)mkdir -p $(VM_SHARE)/icons
	$(Q)mkdir -p $(VM_SHARE)/images
	$(Q)install -m0644 third_party/vidimax/resources/*.json $(VM_SHARE)/
	$(Q)install -m0644 third_party/vidimax/resources/fonts/*.ttf $(VM_SHARE)/fonts/
	$(Q)install -m0644 third_party/vidimax/resources/icons/*.png $(VM_SHARE)/icons/
	$(Q)install -m0644 third_party/vidimax/resources/images/*.png $(VM_SHARE)/images/
	$(Q)install -m0644 third_party/vidimax/resources/images/*.jpg $(VM_SHARE)/images/
endif
ifeq ($(ARCH),)
	rm -rf $(VM_SHARE)/*
	$(Q)mkdir -p $(VM_SHARE)
	$(Q)mkdir -p $(VM_SHARE)/fonts
	$(Q)mkdir -p $(VM_SHARE)/icons
	$(Q)mkdir -p $(VM_SHARE)/images
	$(Q)install -m0644 hd/*.json $(VM_SHARE)/
	$(Q)install -m0644 hd/fonts/*.ttf $(VM_SHARE)/fonts/
	$(Q)install -m0644 hd/icons/*.png $(VM_SHARE)/icons/
	$(Q)install -m0644 hd/images/*.png $(VM_SHARE)/images/
	$(Q)install -m0644 hd/images/*.jpg $(VM_SHARE)/images/
endif


ifneq ($(STAPISDK_VERSION),)
copy_nfs: $(PROGRAM)
	$(Q)mkdir -p $(ROOTFS_NFS)/opt/elecard/lib $(ROOTFS_NFS)/opt/elecard/bin $(ROOTFS_NFS)/opt/elecard/share
	$(Q)install -m0755 $(ROOTFS)/opt/elecard/lib/libelcdrpc.so $(ROOTFS_NFS)/opt/elecard/lib
	$(Q)install -m0755 $(PROGRAM) $(ROOTFS_NFS)/opt/elecard/bin
	rm -rf  $(ROOTFS_NFS)/opt/elecard/share/StbMainApp
	$(Q)cp -r $(DATA_DIR) $(ROOTFS_NFS)/opt/elecard/share/
ifeq ($(ENABLE_VIDIMAX),1)
	rm -rf $(VM_NFS_SHARE)/vidimax
	$(Q)cp -r $(VM_SHARE) $(VM_NFS_SHARE)
endif
ifeq ($(ENABLE_HDDESIGN),1)
	rm -rf $(HD_NFS_SHARE)/hd
	$(Q)cp -r $(HD_SHARE) $(HD_NFS_SHARE)
endif
endif #STAPISDK

copy_images :
	@echo 'INSTALL $(PROGRAM)/images -> $(DATA_DIR)/images'
	rm -f $(DATA_DIR)/images/*
	$(Q)mkdir -p $(DATA_DIR)/images
	$(Q)install -m0644 images/*.png $(DATA_DIR)/images
	$(Q)install -m0644 images/*.jpg $(DATA_DIR)/images || true

copy_config :
	@echo 'INSTALL $(PROGRAM)/streams.txt & channels.conf & settings.conf -> $(DEFAULTS_DIR)'
	$(Q)mkdir -p $(DEFAULTS_DIR)
	$(Q)install -m0644 streams.txt $(DEFAULTS_DIR)
	$(Q)install -m0644 channels.conf $(DEFAULTS_DIR)
	$(Q)install -m0644 settings.conf $(DEFAULTS_DIR)

copy_fonts :
	@echo 'INSTALL $(PROGRAM)/fonts -> $(DATA_DIR)/fonts'
	rm -f $(DATA_DIR)/fonts/*
	$(Q)mkdir -p $(DATA_DIR)/fonts
	$(Q)install -m0644 fonts/*.ttf $(DATA_DIR)/fonts

copy_laguages :
	@echo 'INSTALL $(PROGRAM)/languages -> $(DATA_DIR)/languages'
	rm -f $(DATA_DIR)/languages/*
	$(Q)mkdir -p $(DATA_DIR)/languages
	$(Q)install -m0644 languages/*.lng $(DATA_DIR)/languages

copy_sounds :
	@echo 'INSTALL $(PROGRAM)/sounds -> $(DATA_DIR)/sounds'
	rm -f $(DATA_DIR)/sounds/*
	$(Q)mkdir -p $(DATA_DIR)/sounds
	$(Q)install -m0644 sounds/*.wav $(DATA_DIR)/sounds || true
