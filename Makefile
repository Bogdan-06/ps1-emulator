CXX ?= g++

CPPFLAGS := -Iinclude
CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -O2
DEPFLAGS := -MMD -MP

BUILD_DIR := build
APP := $(BUILD_DIR)/ps1-emulator
TEST_APP := $(BUILD_DIR)/ps1-emulator-tests

CORE_SOURCES := src/bios.cpp src/bus.cpp src/cli.cpp src/cpu.cpp src/gpu.cpp
APP_SOURCES := src/main.cpp $(CORE_SOURCES)
TEST_SOURCES := tests/test_main.cpp tests/cli_tests.cpp tests/bus_tests.cpp tests/cpu_tests.cpp tests/gpu_tests.cpp $(CORE_SOURCES)

APP_OBJECTS := $(APP_SOURCES:%.cpp=$(BUILD_DIR)/%.o)
TEST_OBJECTS := $(TEST_SOURCES:%.cpp=$(BUILD_DIR)/%.o)
DEPS := $(sort $(APP_OBJECTS:.o=.d) $(TEST_OBJECTS:.o=.d))

.PHONY: all test clean

all: $(APP)

test: $(TEST_APP)
	./$(TEST_APP)

$(APP): $(APP_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(TEST_APP): $(TEST_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
