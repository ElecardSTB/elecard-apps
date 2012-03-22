
include ../../etc/envvars.mk
-include $(BUILDROOT)/.prjconfig
-include $(COMPONENT_DIR)/firmwareDesc


all:
	echo "This make should run by buildroot!"

ROOTFS_TARGET=$(BUILDROOT)/rootfs
QMAKE_PATH=$(STAGINGDIR)/usr/bin

MAKE_QMAKE_LINKS = \
	if [ ! -e $(QMAKE_PATH)/sh4-linux-qmake ]; then ln -s qmake $(QMAKE_PATH)/sh4-linux-qmake; fi; \
	if [ ! -e $(QMAKE_PATH)/sh4-linux-moc ]; then ln -s moc $(QMAKE_PATH)/sh4-linux-moc; fi; \
	if [ ! -e $(QMAKE_PATH)/sh4-linux-uic ]; then ln -s uic $(QMAKE_PATH)/sh4-linux-uic; fi; \
	if [ ! -e $(QMAKE_PATH)/sh4-linux-rcc ]; then ln -s rcc $(QMAKE_PATH)/sh4-linux-rcc; fi

#This calls from buildroot for rootfs
rootfs-apps:
	make CROSS_COMPILE=sh4-linux- BUILD_TARGET=sh4/ -C ./elcdRpcLib
	make CC="sh4-linux-gcc --sysroot=$(STAGINGDIR)" BUILD_TARGET=sh4/ prefix=$(ROOTFS_TARGET)/opt/elecard/ -C ./SambaQuery all install
	make CC=sh4-linux-gcc -C ./StbCommandClient
	mkdir -p $(ROOTFS_TARGET)/opt/elecard/lib/ $(ROOTFS_TARGET)/opt/elecard/bin/
	install -m 755 -p ./elcdRpcLib/sh4/libelcdrpc.so $(ROOTFS_TARGET)/opt/elecard/lib/
	install -m 755 -p ./StbCommandClient/StbCommandClient $(ROOTFS_TARGET)/opt/elecard/bin/
ifeq ($(CONFIG_ELECD_ENABLE),y)
	rm -f ./StbMainApp/src/libvidimax.a
	make CROSS_COMPILE=sh4-linux- BUILD_TARGET=sh4/ -C ./StbMainApp/DLNALib all
#	make CROSS_COMPILE=sh4-linux- RELEASE_TYPE="$(ROOTFSVER) built $(DATE_READABLE) @ $(HOSTNAME)" -C ./StbMainApp clean all
	make CROSS_COMPILE=sh4-linux- RELEASE_TYPE="$(ROOTFSVER) built $(DATE_READABLE) @ $(HOSTNAME)" REVISION="$(STBMAINAPPVER_GIT)" -C ./StbMainApp all
#temporary unneed
#	$(call MAKE_QMAKE_LINKS)
#	$(QMAKE_PATH)/sh4-linux-qmake ./maingui/maingui.pro -o ./maingui/Makefile
#	make -C ./maingui all install
endif


rootfs-apps-clean:


help:
	echo "help"
