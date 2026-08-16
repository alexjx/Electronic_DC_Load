CXX ?= g++

BUILD_DIR := /tmp/electronic_dc_load_tests
SANITIZERS ?= -fsanitize=address,undefined -fno-omit-frame-pointer
COMMON_FLAGS := -std=c++11 -Wall -Wextra -Wpedantic -Werror $(SANITIZERS)
STUB_FLAGS := -I$(CURDIR)/stubs -I$(CURDIR)/..

TARGETS := \
	$(BUILD_DIR)/control_test \
	$(BUILD_DIR)/setter_test \
	$(BUILD_DIR)/lm35_test \
	$(BUILD_DIR)/ad7190_test

.PHONY: all test clean

all: test

test: $(TARGETS)
	@set -e; for test_binary in $(TARGETS); do \
		echo "Running $${test_binary}"; \
		"$${test_binary}"; \
	done

$(BUILD_DIR):
	@mkdir -p $@

$(BUILD_DIR)/control_test: control_test.cc ../control.h | $(BUILD_DIR)
	$(CXX) $(COMMON_FLAGS) -I$(CURDIR)/.. $< -o $@

$(BUILD_DIR)/setter_test: setter_test.cc ../setter.h stubs/EEPROM.h | $(BUILD_DIR)
	$(CXX) $(COMMON_FLAGS) $(STUB_FLAGS) $< -o $@

$(BUILD_DIR)/lm35_test: lm35_test.cc ../lm35.h stubs/Arduino.h | $(BUILD_DIR)
	$(CXX) $(COMMON_FLAGS) $(STUB_FLAGS) $< -o $@

$(BUILD_DIR)/ad7190_test: ad7190_test.cc ../ad7190.h stubs/Arduino.h stubs/SPI.h | $(BUILD_DIR)
	$(CXX) $(COMMON_FLAGS) $(STUB_FLAGS) $< -o $@

clean:
	rm -rf $(BUILD_DIR)
