
include ../../etc/envvars.mk
include $(BUILDROOT)/.prjconfig
include $(COMPONENT_DIR)/fwinfo/firmwareDesc

ROOTFS_TARGET=$(BUILDROOT)/rootfs

all:
	echo "This make should run by buildroot!"

build-apps:
	$(call ECHO_MESSAGE,Build src/apps)
	make CROSS_COMPILE=sh4-linux- BUILD_TARGET=sh4/ -C ./elcdRpcLib
	make CROSS_COMPILE=sh4-linux- BUILD_TARGET=sh4/ -C ./cJSON
# elcdrpc and cjson should be installed before building StbMainApp, because this libraries are used by StbMainApp
	install -m 755 -p ./elcdRpcLib/sh4/libelcdrpc.so $(ROOTFS_TARGET)/opt/elecard/lib/
	install -m 755 -p ./cJSON/sh4/libcjson.so $(ROOTFS_TARGET)/opt/elecard/lib/
	make CC="sh4-linux-gcc --sysroot=$(STAGINGDIR)" BUILD_TARGET=sh4/ prefix=$(ROOTFS_TARGET)/opt/elecard/ -C ./SambaQuery all install
	make CC=sh4-linux-gcc -C StbCommandClient
	make CC=sh4-linux-gcc -C mdevmonitor
	make CC=sh4-linux-gcc -C StbPvr all install
ifeq ($(CONFIG_ELECD_ENABLE),y)
	make CROSS_COMPILE=sh4-linux- RELEASE_TYPE="$(REVISION) built on $(HOSTNAME)" COMPILE_TIME="$(DATE_READABLE)" -C ./StbMainApp all
endif

install-apps: build-apps
	mkdir -p $(ROOTFS_TARGET)/opt/elecard/lib/ $(ROOTFS_TARGET)/opt/elecard/bin/
	install -m 755 -p ./StbCommandClient/StbCommandClient $(ROOTFS_TARGET)/opt/elecard/bin/
	install -m 755 -p ./mdevmonitor/mdevmonitor $(ROOTFS_TARGET)/opt/elecard/bin/

#This calls from buildroot for rootfs
rootfs-apps: install-apps

rootfs-apps-clean:

help:
	echo "help"

