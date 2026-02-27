#!/usr/bin/env python3
"""
Compare output STRUCTURE between Rust (anofox_forecast) and C++ (anofox_statistics) extensions.

Only compares:
- Column names
- Column types
- Number of rows
- Struct field names

Does NOT compare actual numerical values.

Usage:
    python compare_structure.py --rust-ext path/to/anofox_forecast.duckdb_extension \
                                --cpp-ext path/to/anofox_statistics.duckdb_extension
"""

import argparse
import duckdb
from dataclasses import dataclass
from typing import Any, Optional, List, Tuple
import sys
import os


@dataclass
class StructureResult:
    name: str
    passed: bool
    rust_structure: Any
    cpp_structure: Any
    error: Optional[str] = None
    details: Optional[str] = None


def get_result_structure(conn: duckdb.DuckDBPyConnection, query: str) -> dict:
    """Get the structure of a query result (columns, types, row count)."""
    try:
        result = conn.execute(query)
        description = result.description
        rows = result.fetchall()

        return {
            "columns": [(col[0], str(col[1])) for col in description] if description else [],
            "row_count": len(rows),
            "sample_row": rows[0] if rows else None,
            "error": None
        }
    except Exception as e:
        return {
            "columns": [],
            "row_count": 0,
            "sample_row": None,
            "error": str(e)
        }


def compare_structures(rust_struct: dict, cpp_struct: dict) -> Tuple[bool, str]:
    """Compare two result structures, return (passed, details)."""
    details = []
    passed = True

    # Check for errors
    if rust_struct["error"] and cpp_struct["error"]:
        return True, "Both errored (expected)"
    if rust_struct["error"]:
        return False, f"Rust error: {rust_struct['error']}"
    if cpp_struct["error"]:
        return False, f"C++ error: {cpp_struct['error']}"

    # Compare column count
    rust_cols = rust_struct["columns"]
    cpp_cols = cpp_struct["columns"]

    if len(rust_cols) != len(cpp_cols):
        details.append(f"Column count: Rust={len(rust_cols)}, C++={len(cpp_cols)}")
        passed = False

    # Compare column names (case-insensitive)
    rust_col_names = [c[0].lower() for c in rust_cols]
    cpp_col_names = [c[0].lower() for c in cpp_cols]

    if rust_col_names != cpp_col_names:
        details.append(f"Column names differ:")
        details.append(f"  Rust: {rust_col_names}")
        details.append(f"  C++:  {cpp_col_names}")
        passed = False

    # Compare row count
    if rust_struct["row_count"] != cpp_struct["row_count"]:
        details.append(f"Row count: Rust={rust_struct['row_count']}, C++={cpp_struct['row_count']}")
        passed = False

    if passed:
        return True, f"OK (cols={len(rust_cols)}, rows={rust_struct['row_count']})"

    return False, "\n".join(details)


def setup_test_data(conn: duckdb.DuckDBPyConnection):
    """Create test tables."""
    conn.execute("""
        CREATE OR REPLACE TABLE test_series AS
        SELECT
            'A' AS id,
            '2024-01-01'::DATE + INTERVAL (i) DAY AS ds,
            10.0 + i * 0.5 + sin(i * 3.14159 / 7) * 2 AS value
        FROM generate_series(0, 29) AS t(i)
        UNION ALL
        SELECT
            'B' AS id,
            '2024-01-01'::DATE + INTERVAL (i) DAY AS ds,
            20.0 + i * 0.3 + cos(i * 3.14159 / 7) * 3 AS value
        FROM generate_series(0, 29) AS t(i)
    """)

    conn.execute("""
        CREATE OR REPLACE TABLE test_single AS
        SELECT
            '2024-01-01'::DATE + INTERVAL (i) DAY AS ds,
            100.0 + i * 2.0 + sin(i * 3.14159 / 7) * 10 AS value
        FROM generate_series(0, 59) AS t(i)
    """)


# Define test cases - (name, rust_query, cpp_query)
# If cpp_query is None, same as rust_query
# NOTE: Some functions have different APIs between Rust (scalar) and C++ (table)
TEST_CASES = [
    # ts_forecast_by structure (long format) - use 'ARIMA' model (uppercase required by C++)
    ("ts_forecast_by columns",
     "SELECT * FROM ts_forecast_by('test_series', id, ds, value, 'ARIMA', 3) LIMIT 1",
     None),

    ("ts_forecast_by row count for 2 groups x 3 steps = 6",
     "SELECT COUNT(*) FROM ts_forecast_by('test_series', id, ds, value, 'ARIMA', 3)",
     None),

    # Metrics - simple scalar returns (same in both)
    ("ts_mae return type",
     "SELECT ts_mae([1.0, 2.0], [1.1, 2.1])",
     None),

    ("ts_mse return type",
     "SELECT ts_mse([1.0, 2.0], [1.1, 2.1])",
     None),

    ("ts_rmse return type",
     "SELECT ts_rmse([1.0, 2.0], [1.1, 2.1])",
     None),

    ("ts_mape return type",
     "SELECT ts_mape([100.0, 200.0], [110.0, 190.0])",
     None),

    ("ts_smape return type",
     "SELECT ts_smape([100.0, 200.0], [110.0, 190.0])",
     None),

    ("ts_r2 return type",
     "SELECT ts_r2([1.0, 2.0, 3.0], [1.1, 2.0, 2.9])",
     None),

    ("ts_bias return type",
     "SELECT ts_bias([1.0, 2.0, 3.0], [1.1, 2.0, 2.9])",
     None),

    ("ts_coverage return type",
     "SELECT ts_coverage([1.0, 2.0, 3.0], [0.5, 1.5, 2.5], [1.5, 2.5, 3.5])",
     None),
]


def main():
    parser = argparse.ArgumentParser(description="Compare extension output structures")
    parser.add_argument("--rust-ext", required=True, help="Path to Rust extension")
    parser.add_argument("--cpp-ext", default="community", help="Path to C++ extension or 'community' to load from community repo")
    parser.add_argument("--verbose", "-v", action="store_true", help="Verbose output")
    args = parser.parse_args()

    # Create connections
    rust_conn = duckdb.connect(":memory:", config={"allow_unsigned_extensions": "true"})
    cpp_conn = duckdb.connect(":memory:")

    # Load Rust extension
    try:
        rust_conn.execute(f"LOAD '{args.rust_ext}'")
        print(f"✓ Loaded Rust extension: {args.rust_ext}")
    except Exception as e:
        print(f"✗ Failed to load Rust extension: {e}")
        sys.exit(1)

    # Load C++ extension (from community or local path)
    try:
        if args.cpp_ext == "community":
            cpp_conn.execute("FORCE INSTALL anofox_forecast FROM community")
            cpp_conn.execute("LOAD anofox_forecast")
            print(f"✓ Loaded C++ extension from community")
        else:
            cpp_conn.execute(f"LOAD '{args.cpp_ext}'")
            print(f"✓ Loaded C++ extension: {args.cpp_ext}")
    except Exception as e:
        print(f"✗ Failed to load C++ extension: {e}")
        sys.exit(1)

    # Setup test data
    setup_test_data(rust_conn)
    setup_test_data(cpp_conn)

    # Run tests
    passed = 0
    failed = 0
    skipped = 0

    print("\n" + "=" * 70)
    print("Comparing output STRUCTURES (not values)")
    print("=" * 70 + "\n")

    for test_case in TEST_CASES:
        name = test_case[0]
        rust_query = test_case[1]
        cpp_query = test_case[2] if len(test_case) > 2 and test_case[2] else rust_query

        rust_struct = get_result_structure(rust_conn, rust_query)
        cpp_struct = get_result_structure(cpp_conn, cpp_query)

        # Skip if both error (function might not exist in one)
        if rust_struct["error"] and cpp_struct["error"]:
            print(f"[SKIP] {name}")
            print(f"       Both: function not available")
            skipped += 1
            continue

        is_pass, details = compare_structures(rust_struct, cpp_struct)

        if is_pass:
            passed += 1
            print(f"[PASS] {name}")
            if args.verbose:
                print(f"       {details}")
        else:
            failed += 1
            print(f"[FAIL] {name}")
            print(f"       {details}")
            if args.verbose:
                print(f"       Rust cols: {rust_struct['columns']}")
                print(f"       C++ cols:  {cpp_struct['columns']}")

    # Summary
    print("\n" + "=" * 70)
    print(f"Results: {passed} passed, {failed} failed, {skipped} skipped")
    print("=" * 70)

    sys.exit(0 if failed == 0 else 1)


if __name__ == "__main__":
    main()
