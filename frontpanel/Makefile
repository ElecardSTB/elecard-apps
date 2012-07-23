
PROGRAM=$(BUILD_TARGET)frontpanel

#src=frontpanel.c
src=$(wildcard *.c)

LOCAL_CFLAGS = -Wall -Wextra
LOCAL_LDFLAGS = -lrt
#$(error "CROSS_COMPILE=$(CROSS_COMPILE)")
all: $(PROGRAM)

$(PROGRAM): $(src) $(BUILD_TARGET)
	$(CROSS_COMPILE)gcc -o $(PROGRAM) $(LOCAL_CFLAGS) $(CFLAGS)  $(LDFLASG) $(src) $(LOCAL_LDFLAGS)

$(BUILD_TARGET):
	mkdir -p $@

install: $(PROGRAM)
	cp -f $(PROGRAM) $(ROOTFS)_nfs/opt/elecard/bin/

.PHONY: all install clean
clean:
	rm -f *.o $(PROGRAM)
