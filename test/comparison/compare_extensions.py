#!/usr/bin/env python3
"""
Compare outputs between Rust (anofox_forecast) and C++ (anofox_statistics) extensions.

Usage:
    python compare_extensions.py --rust-ext path/to/anofox_forecast.duckdb_extension \
                                  --cpp-ext path/to/anofox_statistics.duckdb_extension
"""

import argparse
import duckdb
import numpy as np
from dataclasses import dataclass
from typing import Any, Optional
import sys


@dataclass
class TestResult:
    name: str
    passed: bool
    rust_result: Any
    cpp_result: Any
    error: Optional[str] = None


def compare_values(rust_val, cpp_val, tolerance=1e-6) -> bool:
    """Compare two values with tolerance for floats."""
    if rust_val is None and cpp_val is None:
        return True
    if rust_val is None or cpp_val is None:
        return False

    if isinstance(rust_val, (int, float)) and isinstance(cpp_val, (int, float)):
        if np.isnan(rust_val) and np.isnan(cpp_val):
            return True
        return abs(rust_val - cpp_val) < tolerance

    if isinstance(rust_val, str) and isinstance(cpp_val, str):
        return rust_val == cpp_val

    if isinstance(rust_val, (list, tuple)) and isinstance(cpp_val, (list, tuple)):
        if len(rust_val) != len(cpp_val):
            return False
        return all(compare_values(r, c, tolerance) for r, c in zip(rust_val, cpp_val))

    return rust_val == cpp_val


def run_test(rust_conn: duckdb.DuckDBPyConnection,
             cpp_conn: duckdb.DuckDBPyConnection,
             name: str,
             query: str,
             setup_query: Optional[str] = None) -> TestResult:
    """Run a test query on both connections and compare results."""
    try:
        if setup_query:
            try:
                rust_conn.execute(setup_query)
            except:
                pass
            try:
                cpp_conn.execute(setup_query)
            except:
                pass

        rust_result = rust_conn.execute(query).fetchall()
        cpp_result = cpp_conn.execute(query).fetchall()

        passed = compare_values(rust_result, cpp_result)

        return TestResult(
            name=name,
            passed=passed,
            rust_result=rust_result,
            cpp_result=cpp_result
        )
    except Exception as e:
        return TestResult(
            name=name,
            passed=False,
            rust_result=None,
            cpp_result=None,
            error=str(e)
        )


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


# Define test cases
TEST_CASES = [
    # Metrics tests
    ("ts_mae basic", "SELECT round(ts_mae([1.0, 2.0, 3.0], [1.1, 2.2, 2.9]), 4)"),
    ("ts_mse basic", "SELECT round(ts_mse([1.0, 2.0, 3.0], [1.1, 2.2, 2.9]), 4)"),
    ("ts_rmse basic", "SELECT round(ts_rmse([1.0, 2.0, 3.0], [1.1, 2.2, 2.9]), 4)"),
    ("ts_mape basic", "SELECT round(ts_mape([100.0, 200.0, 300.0], [110.0, 190.0, 310.0]), 4)"),
    ("ts_smape basic", "SELECT round(ts_smape([100.0, 200.0, 300.0], [110.0, 190.0, 310.0]), 4)"),
    ("ts_r2 basic", "SELECT round(ts_r2([1.0, 2.0, 3.0, 4.0], [1.1, 2.0, 2.9, 4.1]), 4)"),

    # Imputation tests
    ("ts_fill_nulls_const", "SELECT ts_fill_nulls_const([1.0, NULL, 3.0], 0.0)"),
    ("ts_fill_nulls_forward", "SELECT ts_fill_nulls_forward([1.0, NULL, NULL, 4.0])"),
    ("ts_fill_nulls_backward", "SELECT ts_fill_nulls_backward([NULL, NULL, 3.0, 4.0])"),
    ("ts_fill_nulls_mean", "SELECT ts_fill_nulls_mean([1.0, NULL, 3.0])"),

    # Filter tests
    ("ts_diff order 1", "SELECT ts_diff([1.0, 3.0, 6.0, 10.0], 1)"),
    ("ts_is_constant true", "SELECT ts_is_constant([5.0, 5.0, 5.0, 5.0])"),
    ("ts_is_constant false", "SELECT ts_is_constant([5.0, 5.0, 5.1, 5.0])"),
    ("ts_drop_leading_zeros", "SELECT ts_drop_leading_zeros([0.0, 0.0, 1.0, 2.0, 3.0])"),
    ("ts_drop_trailing_zeros", "SELECT ts_drop_trailing_zeros([1.0, 2.0, 3.0, 0.0, 0.0])"),

    # Stats tests
    ("ts_stats length", """
        SELECT (stats).length FROM (
            SELECT ts_stats([1.0, 2.0, 3.0, 4.0, 5.0]) AS stats
        )
    """),
    ("ts_stats mean", """
        SELECT round((stats).mean, 4) FROM (
            SELECT ts_stats([1.0, 2.0, 3.0, 4.0, 5.0]) AS stats
        )
    """),

    # Seasonality tests
    ("ts_detect_seasonality", """
        WITH seasonal_data AS (
            SELECT sin(i * 3.14159 * 2 / 12.0) * 10 + 50 AS value
            FROM generate_series(0, 119) AS t(i)
        )
        SELECT ts_detect_seasonality(LIST(value), 24) > 0
        FROM seasonal_data
    """),

    # Forecast tests - check count of forecasts
    ("ts_forecast count", """
        SELECT len((fcst).point) FROM (
            SELECT ts_forecast(LIST(value ORDER BY ds), 5, 'naive') AS fcst
            FROM test_single
        )
    """),

    # ts_forecast_by - check row count (long format)
    ("ts_forecast_by row count", """
        SELECT COUNT(*) FROM ts_forecast_by('test_series', id, ds, value, 'naive', 3, NULL)
    """),

    ("ts_forecast_by distinct steps", """
        SELECT COUNT(DISTINCT forecast_step) FROM ts_forecast_by('test_series', id, ds, value, 'naive', 3, NULL)
    """),
]


def main():
    parser = argparse.ArgumentParser(description="Compare Rust and C++ extension outputs")
    parser.add_argument("--rust-ext", required=True, help="Path to Rust extension")
    parser.add_argument("--cpp-ext", required=True, help="Path to C++ extension")
    parser.add_argument("--verbose", "-v", action="store_true", help="Verbose output")
    args = parser.parse_args()

    # Create connections
    rust_conn = duckdb.connect(":memory:")
    cpp_conn = duckdb.connect(":memory:")

    # Load extensions
    try:
        rust_conn.execute(f"LOAD '{args.rust_ext}'")
        print(f"Loaded Rust extension: {args.rust_ext}")
    except Exception as e:
        print(f"Failed to load Rust extension: {e}")
        sys.exit(1)

    try:
        cpp_conn.execute(f"LOAD '{args.cpp_ext}'")
        print(f"Loaded C++ extension: {args.cpp_ext}")
    except Exception as e:
        print(f"Failed to load C++ extension: {e}")
        sys.exit(1)

    # Setup test data
    setup_test_data(rust_conn)
    setup_test_data(cpp_conn)

    # Run tests
    results = []
    passed = 0
    failed = 0

    print("\n" + "=" * 60)
    print("Running comparison tests...")
    print("=" * 60 + "\n")

    for name, query in TEST_CASES:
        result = run_test(rust_conn, cpp_conn, name, query)
        results.append(result)

        if result.passed:
            passed += 1
            status = "PASS"
        else:
            failed += 1
            status = "FAIL"

        print(f"[{status}] {name}")

        if args.verbose or not result.passed:
            if result.error:
                print(f"       Error: {result.error}")
            else:
                print(f"       Rust:  {result.rust_result}")
                print(f"       C++:   {result.cpp_result}")

    # Summary
    print("\n" + "=" * 60)
    print(f"Results: {passed} passed, {failed} failed out of {len(TEST_CASES)} tests")
    print("=" * 60)

    # Return exit code
    sys.exit(0 if failed == 0 else 1)


if __name__ == "__main__":
    main()
