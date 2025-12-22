#!/bin/bash
# Run function comparison tests between Rust port and C++ community extension

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"

echo "Anofox Forecast Extension - Comparison Test Runner"
echo "=================================================="

# Check if build exists
if [ ! -f "$BUILD_DIR/extension/anofox_forecast/anofox_forecast.duckdb_extension" ]; then
    echo "ERROR: Rust extension not found. Building..."
    cd "$BUILD_DIR"
    make -j$(nproc)
fi

# Run Python comparison script
echo ""
echo "Running comparison tests..."
echo ""

cd "$SCRIPT_DIR"
python3 compare_functions.py

echo ""
echo "Done!"
