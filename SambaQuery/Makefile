
PROGRAM = $(BUILD_TARGET)SambaQuery

prefix ?= /opt/elecard

SRC = $(wildcard src/*.c)
HDR = $(wildcard src/*.h include/*.h)

LOCAL_CFLAGS  = -Iinclude -O3 -s -Wall -Wextra
LOCAL_LDFLAGS = -lsmbclient

all: $(PROGRAM)

$(PROGRAM): $(SRC) $(HDR)
	if [ "$(BUILD_TARGET)" -a ! -d "$(BUILD_TARGET)" ]; then mkdir -p $(BUILD_TARGET); fi
	$(CC) -o $(PROGRAM) $(LOCAL_CFLAGS) $(CFLAGS) $(LOCAL_LDFLAGS) $(LDFLAGS) $(SRC)

install: $(PROGRAM)
	install -m0755 $(PROGRAM) $(prefix)/bin/

clean:
	rm -f $(OBJ) $(PROGRAM)
