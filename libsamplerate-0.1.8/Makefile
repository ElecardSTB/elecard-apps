
LIBSUFFIX = .a
DLLSUFFIX = .so

LIB = libsamplerate

LOCAL_PATH= $(call my-dir)

CROSS_COMPILE=sh4-linux-

SRC = src/samplerate.c src/src_sinc.c  

HDR = 

BUILD_TARGET= sh4/
OUT = $(BUILD_TARGET)$(LIB)

OBJ = $(addsuffix .o, $(notdir $(basename $(SRC))))

MYCFLAGS = -Iinclude -O3 -g -s -Wall -Wextra
MYCFLAGS += -Wno-sequence-point -Wno-extra

ifneq ($(BUILD_TARGET),)
    CC=${CROSS_COMPILE}gcc
    AR=${CROSS_COMPILE}ar
    LD=${CROSS_COMPILE}gcc
endif

all: $(OUT)$(DLLSUFFIX) $(OUT)$(LIBSUFFIX)

$(OUT)$(DLLSUFFIX): $(SRC) $(HDR)
	if [ "$(BUILD_TARGET)" -a ! -d "$(BUILD_TARGET)" ]; then mkdir -p $(BUILD_TARGET); fi
	$(CC) $(CFLAGS) $(MYCFLAGS) -fPIC -c $(SRC)
	$(LD) -shared -o $@ $(OBJ)
	rm $(OBJ)

$(OUT)$(LIBSUFFIX): $(SRC) $(HDR)
	if [ "$(BUILD_TARGET)" -a ! -d "$(BUILD_TARGET)" ]; then mkdir -p $(BUILD_TARGET); fi
	$(CC) $(CFLAGS) $(MYCFLAGS) -c $(SRC)
	$(AR) rcs $@ $(OBJ)
	rm $(OBJ)

clean:
	rm -f $(OBJ) $(OUT)$(DLLSUFFIX) $(OUT)$(LIBSUFFIX)
	if [ "$(BUILD_TARGET)" ]; then rm -f $(BUILD_TARGET)*; fi

#include $(BUILD_STATIC_LIBRARY)
