PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Configuration of extension
EXT_NAME=anofox_forecast
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile

# Rust targets (for local development)
.PHONY: rust rust_debug rust_test fmt check header benchmark-setup benchmark-m4 benchmark-m5

rust:
	cargo build --release

rust_debug:
	cargo build

rust_test:
	cargo test

fmt:
	cargo fmt

check:
	cargo check
	cargo clippy

header:
	cargo build --release
	@echo "Header generated at src/include/anofox_fcst_ffi.h"

# Benchmark targets
benchmark-setup:
	cd benchmark && uv sync

benchmark-m4:
	cd benchmark && uv run python run_all_benchmarks.py --dataset m4 --group Daily

benchmark-m5:
	cd benchmark && uv run python run_all_benchmarks.py --dataset m5 --group Daily

# Clean everything including Rust
clean_all:
	rm -rf build
	cargo clean
