#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Configuration - adjust these to match your extension
EXTENSION_NAME="${EXTENSION_NAME:-anofox_forecast}"
BUILD_DIR="${BUILD_DIR:-./build/release}"
SQL_DIR="${PROJECT_ROOT}/test/sql/docs_examples"

# Test a SQL file
test_sql_file() {
    local sql_file=$1
    local filename=$(basename "$sql_file")

    # Create temporary file with extension setup
    local test_file=$(mktemp)

    cat > "$test_file" <<EOF
-- Load extension if available
.bail on
INSTALL '${BUILD_DIR}/extension/${EXTENSION_NAME}/${EXTENSION_NAME}.duckdb_extension';
LOAD ${EXTENSION_NAME};

-- Run actual SQL
EOF

    cat "$sql_file" >> "$test_file"

    # Run test
    if duckdb :memory: < "$test_file" > /dev/null 2>&1; then
        echo "  ✅ $filename"
        rm "$test_file"
        return 0
    else
        echo "  ❌ $filename FAILED"
        echo "     Error output:"
        duckdb :memory: < "$test_file" 2>&1 | head -n 20
        rm "$test_file"
        return 1
    fi
}

# Main function
main() {
    echo "🧪 Testing SQL example files..."
    echo ""

    # Check if docs_examples directory exists
    if [ ! -d "$SQL_DIR" ]; then
        echo "⚠️  No docs_examples directory found at $SQL_DIR"
        echo "ℹ️  Skipping SQL example tests"
        return 0
    fi

    local total=0
    local failed=0

    # Test all SQL files in test/sql/docs_examples/
    while IFS= read -r sql_file; do
        ((total++))
        if ! test_sql_file "$sql_file"; then
            ((failed++))
        fi
    done < <(find "$SQL_DIR" -name "*.sql" -type f 2>/dev/null)

    if [ $total -eq 0 ]; then
        echo "⚠️  No SQL example files found"
        return 0
    fi

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
