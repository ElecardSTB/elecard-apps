
LIBSUFFIX = .a
DLLSUFFIX = .so

OUT = $(BUILD_TARGET)libelcdrpc
SRC = $(wildcard src/*.c)
HDR = $(wildcard include/*.h)
OBJ = $(addsuffix .o, $(notdir $(basename $(SRC))))
MYCFLAGS = -I../StbMainApp/src -I../cJSON/include -Iinclude -O3 -g -s -Wall -Wextra -Wno-unused-parameter -Wno-multichar

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
	if [ "$(BUILD_TARGET)" ]; then cp $(HDR) $(BUILD_TARGET); fi
	rm $(OBJ)

$(OUT)$(LIBSUFFIX): $(SRC) $(HDR)
	if [ "$(BUILD_TARGET)" -a ! -d "$(BUILD_TARGET)" ]; then mkdir -p $(BUILD_TARGET); fi
	$(CC) $(CFLAGS) $(MYCFLAGS) -c $(SRC)
	$(AR) rcs $@ $(OBJ)
	if [ "$(BUILD_TARGET)" ]; then cp $(HDR) $(BUILD_TARGET); fi
	rm $(OBJ)

clean:
	rm -f $(OBJ) $(OUT)$(DLLSUFFIX) $(OUT)$(LIBSUFFIX)
	if [ "$(BUILD_TARGET)" ]; then rm -f $(BUILD_TARGET)*; fi
