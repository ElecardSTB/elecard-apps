
PROGRAM=fb_logo

src=fb_logo.c

LOCAL_CFLAGS  = -Wall -Wextra
LOCAL_LDFLAGS =

FB_LOGO_ANIMATE_ELECARD_LOGO ?= 1
ENABLE_FUSION ?= 0

# To set logo file use environment:
#  FB_LOGO_USE_FILE=/opt/elecard/share/elecard_logo.bmp
# And put bmp file to appropriate path in $PRJROOT/src/initramfs/overlay. For example: 
#  $PRJROOT/src/initramfs/overlay/opt/elecard/share/elecard_logo.bmp
ifneq ($(FB_LOGO_USE_FILE),)
	LOCAL_CFLAGS += -DUSE_BITMAP_FILE=\"$(FB_LOGO_USE_FILE)\"
endif

ifeq ($(ENABLE_FUSION),1)
	LOCAL_CFLAGS += -DENABLE_FUSION
else
ifeq ($(FB_LOGO_ANIMATE_ELECARD_LOGO),1)
	LOCAL_CFLAGS += -DANIMATE_ELECARD_LOGO
endif
endif


CFLAGS += $(LOCAL_CFLAGS)
LDFLASG += $(LOCAL_LDFLAGS)

all: $(wildcard *.c)
	$(CROSS_COMPILE)gcc -o $(PROGRAM) $(CFLAGS) $(LDFLASG) $(src)

install: $(PROGRAM)
	cp -f $(PROGRAM) $(ROOTFS)_nfs/opt/elecard/bin/

.PHONY: clean
clean:
	rm -f *.o $(PROGRAM)
