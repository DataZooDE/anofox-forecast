#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Configuration - adjust these to match your extension
EXTENSION_NAME="${EXTENSION_NAME:-anofox_forecast}"
BUILD_DIR="${BUILD_DIR:-${PROJECT_ROOT}/build/release}"
SQL_DIR="${PROJECT_ROOT}/test/sql"
DUCKDB_CLI="${DUCKDB_CLI:-$HOME/.local/bin/duckdb}"

# Test a SQL file
test_sql_file() {
    local sql_file=$1
    local filename=$(basename "$sql_file")
    local test_num=$2
    local total_tests=$3

    # Create temporary file with extension setup
    local test_file=$(mktemp)

    # Check if SQL file already has a LOAD statement
    if grep -q "^LOAD.*${EXTENSION_NAME}" "$sql_file"; then
        # File has its own LOAD statement, just use it as-is
        cat > "$test_file" <<EOF
.bail on
EOF
        cat "$sql_file" >> "$test_file"
    else
        # File doesn't have LOAD statement, add one
        cat > "$test_file" <<EOF
-- Load extension if available
.bail on
LOAD '${BUILD_DIR}/extension/${EXTENSION_NAME}/${EXTENSION_NAME}.duckdb_extension';

-- Run actual SQL
EOF
        cat "$sql_file" >> "$test_file"
    fi

    # Show which file is being tested
    echo "  [$test_num/$total_tests] Testing $filename..."

    # Run test
    if "$DUCKDB_CLI" -unsigned :memory: < "$test_file" > /dev/null 2>&1; then
        echo "  [$test_num/$total_tests] ✅ $filename"
        rm "$test_file"
        return 0
    else
        echo "  ❌ $filename FAILED"
        echo "     Error output:"
        "$DUCKDB_CLI" -unsigned :memory: < "$test_file" 2>&1 | head -n 20
        rm "$test_file"
        return 1
    fi
}

# Main function
main() {
    echo "🧪 Testing SQL example files..."
    echo ""

    # Check if DuckDB CLI exists
    if [ ! -f "$DUCKDB_CLI" ]; then
        echo "⚠  DuckDB CLI not found at: $DUCKDB_CLI"
        echo "   Set DUCKDB_CLI environment variable or install DuckDB"
        exit 1
    fi

    # Check if extension exists
    if [ ! -f "${BUILD_DIR}/extension/${EXTENSION_NAME}/${EXTENSION_NAME}.duckdb_extension" ]; then
        echo "⚠  Extension not found at: ${BUILD_DIR}/extension/${EXTENSION_NAME}/${EXTENSION_NAME}.duckdb_extension"
        echo "   Build the extension first with: make release"
        exit 1
    fi

    local total=0
    local failed=0
    local file_count=0

    # Count files first
    file_count=$(find "$SQL_DIR" -name "*.sql" -type f 2>/dev/null | wc -l)
    echo "Found $file_count SQL test files to run"
    echo ""

    # Test all SQL files using command substitution in for loop
    for sql_file in $(find "$SQL_DIR" -name "*.sql" -type f 2>/dev/null | sort); do
        total=$((total + 1))
        test_sql_file "$sql_file" "$total" "$file_count" && true || failed=$((failed + 1))
    done

    echo ""
    echo "================================"
    echo "Total: $total"
    echo "Passed: $((total - failed))"
    echo "Failed: $failed"
    echo "================================"

    if [ $failed -gt 0 ]; then
        exit 1
    fi
}

main "$@"