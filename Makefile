# Makefile for Gitlite

CMAKE = cmake
MAKE = make
BUILD_DIR = build

all: $(BUILD_DIR)/gitlite

$(BUILD_DIR)/gitlite:
	@if [ ! -d $(BUILD_DIR) ]; then mkdir -p $(BUILD_DIR); fi
	cd $(BUILD_DIR) && $(CMAKE) .. && $(MAKE)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean