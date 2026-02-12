#!/usr/bin/env bash
# test-query.sh - Run SQL against DuckDB with the anofox_forecast extension loaded.
# Usage: bash test-query.sh "SELECT * FROM ts_forecast_by(...)"

set -euo pipefail

QUERY="${1:?Usage: bash test-query.sh \"SQL QUERY\"}"

# Locate DuckDB binary: prefer project build, then PATH
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"

if [[ -x "$PROJECT_ROOT/build/release/duckdb" ]]; then
    DUCKDB="$PROJECT_ROOT/build/release/duckdb"
elif command -v duckdb &>/dev/null; then
    DUCKDB="$(command -v duckdb)"
else
    echo "ERROR: DuckDB binary not found." >&2
    echo "  Checked: $PROJECT_ROOT/build/release/duckdb" >&2
    echo "  Also checked: PATH" >&2
    exit 1
fi

# Locate extension binary
EXT_PATH="$PROJECT_ROOT/build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension"
if [[ ! -f "$EXT_PATH" ]]; then
    echo "ERROR: Extension not found at $EXT_PATH" >&2
    echo "  Run: cmake --build build/release" >&2
    exit 1
fi

# Run query in-memory with extension loaded
"$DUCKDB" -unsigned -c "
LOAD '$EXT_PATH';
$QUERY
"
