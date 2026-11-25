CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2
LDLIBS ?= -lm
TARGET = datakit

.PHONY: all clean

all: $(TARGET)

$(TARGET): datakit.c
	$(CC) $(CFLAGS) datakit.c -o $(TARGET) $(LDLIBS)

clean:
	rm -f $(TARGET) $(TARGET).exe
