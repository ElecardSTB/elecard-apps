
PROGRAM=fb_logo

src=fb_logo.c

LOCAL_CFLAGS  = -Wall -Wextra
LOCAL_LDFLAGS =

all: $(wildcard *.c)
	$(CROSS_COMPILE)gcc -o $(PROGRAM) $(LOCAL_CFLAGS) $(CFLAGS) $(LOCAL_LDFLAGS) $(LDFLASG) $(src)

install: $(PROGRAM)
	cp -f $(PROGRAM) $(ROOTFS)_nfs/opt/elecard/bin/

.PHONY: clean
clean:
	rm -f *.o $(PROGRAM)
