
PROGRAM=$(BUILD_TARGET)sysidConverter

src=$(wildcard *.c)

#C_SOURCES := i2c.c main.c

LOCAL_CFLAGS = -Wall -Wextra -I$(KDIR)/include
LOCAL_LDFLAGS = -lrt -lpthread
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
