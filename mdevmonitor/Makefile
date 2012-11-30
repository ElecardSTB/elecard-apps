
PROGRAM = mdevmonitor
C_SOURCES = $(wildcard src/*.c)
CFLAGS += -std=gnu99 -Wall -Wextra

all: $(PROGRAM)

$(PROGRAM): $(C_SOURCES)
	$(CC) -o $@ $(CFLAGS) -fPIC $(C_SOURCES)

clean:
	rm -f $(PROGRAM)
