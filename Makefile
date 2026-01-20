# Kate Code Plugin Makefile

BUILD_DIR := build
BUILD_TYPE ?= Release

.PHONY: build install package clean

build:
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
	cmake --build $(BUILD_DIR)

install: build
	sudo cmake --install $(BUILD_DIR)

package:
	./build-packages.sh

clean:
	rm -rf $(BUILD_DIR)
