# Convenience wrapper around CMake. The real build lives in CMakeLists.txt.
#
#   make              # configure + Release build in ./build
#   make test         # ctest + cpups --self-test
#   make run ARGS="--json"
#
# Overrides: BUILD_TYPE=Debug PREFIX=/opt/local ARGS="--monitor"

BUILD_DIR   = build
BUILD_TYPE ?= Release
PREFIX     ?= /usr/local
ARGS       ?=

CMAKE_FLAGS = -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DCMAKE_INSTALL_PREFIX=$(PREFIX)
CPUPS       = $(BUILD_DIR)/cpups

.PHONY: all build clean distclean test install run monitor json help

all: build

$(BUILD_DIR)/CMakeCache.txt:
	cmake -S . -B $(BUILD_DIR) $(CMAKE_FLAGS)

build: $(BUILD_DIR)/CMakeCache.txt
	cmake --build $(BUILD_DIR) --config $(BUILD_TYPE)

clean:
	@if [ -f "$(BUILD_DIR)/CMakeCache.txt" ]; then \
		cmake --build $(BUILD_DIR) --target clean --config $(BUILD_TYPE); \
	else \
		echo "nothing to clean (no $(BUILD_DIR)/CMakeCache.txt)"; \
	fi

distclean:
	rm -rf "$(BUILD_DIR)"

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure
	$(CPUPS) --self-test

install: build
	cmake --install $(BUILD_DIR) --prefix "$(PREFIX)" --config $(BUILD_TYPE)

run: build
	$(CPUPS) $(ARGS)

monitor: build
	$(CPUPS) --monitor $(ARGS)

json: build
	$(CPUPS) --json $(ARGS)

help:
	@echo "Targets:"
	@echo "  all / build   configure (if needed) and build in $(BUILD_DIR)/  [BUILD_TYPE=$(BUILD_TYPE)]"
	@echo "  clean         remove compiled artifacts; keep $(BUILD_DIR)/"
	@echo "  distclean     delete $(BUILD_DIR)/"
	@echo "  test          build, then ctest and $(CPUPS) --self-test"
	@echo "  install       build, then cmake --install  [PREFIX=$(PREFIX)]"
	@echo "  run           build, then $(CPUPS) \$$ARGS"
	@echo "  monitor       build, then $(CPUPS) --monitor \$$ARGS"
	@echo "  json          build, then $(CPUPS) --json \$$ARGS"
	@echo "  help          this list"
	@echo ""
	@echo "Examples:"
	@echo "  make"
	@echo "  make BUILD_TYPE=Debug"
	@echo "  make run ARGS='--rating'"
	@echo "  make install PREFIX=/usr/local"
