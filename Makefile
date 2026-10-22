PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Configuration of extension
EXT_NAME=anofox_forecast
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile

# Rust targets (for local development)
.PHONY: rust rust_debug rust_test fmt check header audit check-docs benchmark-setup benchmark-m4 benchmark-m5

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

# Phase 18 (RUST-10, D-04/D-05): no-statistics-in-C++/SQL audit + D-03 length-only sweep
# Phase 19 (#272, DIST-01/DIST-02): WASM duckdb_signature section parser self-test.
# This target is wired into .github/workflows/audit.yml (no build required).
audit:
	python3 scripts/audit_no_stats_in_cpp.py --self-test
	python3 scripts/audit_no_stats_in_cpp.py
	python3 scripts/check_length_only_tests.py
	python3 scripts/wasm_signature_sections.py --self-test

# Phase 18 (DOCS-01, D-06): docs/skills SQL snippet + function-name checker
# WR-04 (Phase 18 review): requires `make release` first (runs snippets against
# build/release/duckdb), so this and check_correctness_inventory.py are NOT in
# any CI workflow -- they are required manual release Definition-of-Done steps.
# Run both by hand before every release/tag:
#   GEN=ninja make release && make check-docs && python3 scripts/check_correctness_inventory.py
check-docs:
	python3 scripts/check_doc_snippets.py

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
