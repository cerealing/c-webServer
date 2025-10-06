CXX     ?= g++
BUILD   ?= build
TARGET  := $(BUILD)/maild

SRC := $(wildcard src/*.cpp) $(wildcard src/services/*.cpp)
CXXFLAGS ?= -std=c++20 -O2 -g -D_GNU_SOURCE -Iinclude -Wall -Wextra -Wpedantic
LDFLAGS ?= -lpthread

ifdef USE_REAL_MYSQL
MYSQL_CFLAGS ?= $(shell mysql_config --cflags 2>/dev/null)
MYSQL_LIBS   ?= $(shell mysql_config --libs 2>/dev/null)
CXXFLAGS += -DUSE_REAL_MYSQL $(MYSQL_CFLAGS)
LDFLAGS += $(MYSQL_LIBS)
endif

$(TARGET): $(SRC) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(SRC) -o $@ $(LDFLAGS)

$(BUILD):
	@mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)

run: $(TARGET)
	./$(TARGET) --config config/dev_stub.json

.PHONY: clean run
