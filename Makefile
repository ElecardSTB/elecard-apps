
include ../../etc/envvars.mk
-include $(BUILDROOT)/.prjconfig
-include $(COMPONENT_DIR)/firmwareDesc

ROOTFS_TARGET=$(BUILDROOT)/rootfs

all:
	echo "This make should run by buildroot!"

build-apps:
	make CROSS_COMPILE=sh4-linux- BUILD_TARGET=sh4/ -C ./elcdRpcLib
	make CC="sh4-linux-gcc --sysroot=$(STAGINGDIR)" BUILD_TARGET=sh4/ prefix=$(ROOTFS_TARGET)/opt/elecard/ -C ./SambaQuery all install
	make CC=sh4-linux-gcc -C StbCommandClient
ifeq ($(CONFIG_ELECD_ENABLE),y)
	rm -f ./StbMainApp/src/libvidimax.a
	make CROSS_COMPILE=sh4-linux- BUILD_TARGET=sh4/ -C ./StbMainApp/DLNALib all
	make CROSS_COMPILE=sh4-linux- RELEASE_TYPE="$(ROOTFSVER) built $(DATE_READABLE) @ $(HOSTNAME)" REVISION="$(STBMAINAPPVER_GIT)" -C ./StbMainApp all
endif

install-apps: build-apps
	mkdir -p $(ROOTFS_TARGET)/opt/elecard/lib/ $(ROOTFS_TARGET)/opt/elecard/bin/
	install -m 755 -p ./elcdRpcLib/sh4/libelcdrpc.so $(ROOTFS_TARGET)/opt/elecard/lib/
	install -m 755 -p ./StbCommandClient/StbCommandClient $(ROOTFS_TARGET)/opt/elecard/bin/

#This calls from buildroot for rootfs
rootfs-apps: install-apps

rootfs-apps-clean:

help:
	echo "help"

