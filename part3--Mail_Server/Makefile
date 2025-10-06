CC      ?= gcc
BUILD   ?= build
TARGET  := $(BUILD)/maild

SRC := $(wildcard src/*.c) $(wildcard src/services/*.c)
CFLAGS ?= -std=c11 -O2 -g -D_GNU_SOURCE -Iinclude -Wall
LDFLAGS ?= -lpthread

ifdef USE_REAL_MYSQL
MYSQL_CFLAGS ?= $(shell mysql_config --cflags 2>/dev/null)
MYSQL_LIBS   ?= $(shell mysql_config --libs 2>/dev/null)
CFLAGS += -DUSE_REAL_MYSQL $(MYSQL_CFLAGS)
LDFLAGS += $(MYSQL_LIBS)
endif

$(TARGET): $(SRC) | $(BUILD)
	$(CC) $(CFLAGS) $(SRC) -o $@ $(LDFLAGS)

$(BUILD):
	@mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)

run: $(TARGET)
	./$(TARGET) --config config/dev_stub.json

.PHONY: clean run
