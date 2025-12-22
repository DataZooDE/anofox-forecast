.PHONY: all clean release debug configure test rust benchmark-setup benchmark-m4 benchmark-m5

GENERATOR := Ninja
BUILD_DIR := build
EXTENSION_NAME := anofox_forecast
PROJECT_DIR := $(shell pwd)
DUCKDB_DIR := $(PROJECT_DIR)/duckdb

# Default target
all: release

# Configure with CMake using Ninja (building via DuckDB)
configure:
	@mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake -G "$(GENERATOR)" \
		-DCMAKE_BUILD_TYPE=Release \
		-DEXTENSION_STATIC_BUILD=1 \
		-DDUCKDB_EXTENSION_CONFIGS="$(PROJECT_DIR)/extension_config.cmake" \
		$(DUCKDB_DIR)

configure-debug:
	@mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake -G "$(GENERATOR)" \
		-DCMAKE_BUILD_TYPE=Debug \
		-DEXTENSION_STATIC_BUILD=1 \
		-DDUCKDB_EXTENSION_CONFIGS="$(PROJECT_DIR)/extension_config.cmake" \
		$(DUCKDB_DIR)

# Build release
release: configure
	cd $(BUILD_DIR) && ninja

# Build debug
debug: configure-debug
	cd $(BUILD_DIR) && ninja

# Build Rust crates only
rust:
	cargo build --release

rust-debug:
	cargo build

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)
	cargo clean

# Run Rust tests
test-rust:
	cargo test

# Format code
fmt:
	cargo fmt

# Check code
check:
	cargo check
	cargo clippy

# Generate C header
header:
	cargo build --release
	@echo "Header generated at src/include/anofox_fcst_ffi.h"

# Install extension to DuckDB
install:
	@echo "Extension built at: $(BUILD_DIR)/extension/$(EXTENSION_NAME)/$(EXTENSION_NAME).duckdb_extension"
	@echo "Copy to ~/.duckdb/extensions/v1.4.3/linux_amd64/ to install"

# Test with DuckDB
test:
	cd $(BUILD_DIR) && ./duckdb -c "LOAD '$(EXTENSION_NAME)'; SELECT ts_stats([1.0, 2.0, 3.0, 4.0, 5.0]);"

# Benchmark targets
benchmark-setup:
	cd benchmark && uv sync

benchmark-m4:
	cd benchmark && uv run python run_all_benchmarks.py --dataset m4 --group Daily

benchmark-m5:
	cd benchmark && uv run python run_all_benchmarks.py --dataset m5 --group Daily

# Help
help:
	@echo "Available targets:"
	@echo "  all             - Build release (default)"
	@echo "  release         - Build release version with Ninja"
	@echo "  debug           - Build debug version with Ninja"
	@echo "  rust            - Build Rust crates only (release)"
	@echo "  rust-debug      - Build Rust crates only (debug)"
	@echo "  clean           - Remove build artifacts"
	@echo "  test-rust       - Run Rust tests"
	@echo "  test            - Test extension in DuckDB"
	@echo "  fmt             - Format Rust code"
	@echo "  check           - Check and lint code"
	@echo "  header          - Generate C header from Rust"
	@echo "  install         - Show install instructions"
	@echo "  benchmark-setup - Install benchmark Python dependencies"
	@echo "  benchmark-m4    - Run M4 benchmarks (Daily)"
	@echo "  benchmark-m5    - Run M5 benchmarks (Daily)"
	@echo "  help            - Show this help"
