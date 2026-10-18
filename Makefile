PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

TARGET := YT-Blocker.elf
SRCS := source/main.c source/sqlite3.c source/cJSON.c
OBJS := $(SRCS:.c=.o)

CFLAGS += -O2 -Iinclude \
          -DSQLITE_THREADSAFE=0 \
          -DSQLITE_OMIT_LOAD_EXTENSION=1 \
          -DSQLITE_ENABLE_JSON1 \
          -DHAVE_POSIX_FALLOCATE=0

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS)

clean:
	rm -f $(TARGET) $(OBJS)