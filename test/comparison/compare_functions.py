#!/usr/bin/env python3
"""
Compare all functions between Rust port and C++ community extension.
Ensures 100% API compatibility with matching outputs.
Uses DuckDB CLI with JSON output to avoid Python binding version issues.
"""

import json
import math
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from typing import Any, Optional
from pathlib import Path

# Tolerance for floating point comparisons
FLOAT_EPSILON = 1e-9

@dataclass
class TestResult:
    function_name: str
    passed: bool
    cpp_result: Any
    rust_result: Any
    error_message: Optional[str] = None

def compare_floats(a: float, b: float, epsilon: float = FLOAT_EPSILON) -> bool:
    """Compare two floats within tolerance."""
    if a is None and b is None:
        return True
    if a is None or b is None:
        return False
    if math.isnan(a) and math.isnan(b):
        return True
    if math.isinf(a) and math.isinf(b):
        return a > 0 == b > 0
    return abs(a - b) <= epsilon * max(1.0, abs(a), abs(b))

def compare_values(a: Any, b: Any, path: str = "") -> tuple[bool, str]:
    """Recursively compare two values."""
    if a is None and b is None:
        return True, ""
    if a is None or b is None:
        return False, f"{path}: one is None (cpp={a}, rust={b})"

    # Handle floats
    if isinstance(a, float) and isinstance(b, float):
        if not compare_floats(a, b):
            return False, f"{path}: float mismatch (cpp={a}, rust={b}, diff={abs(a-b)})"
        return True, ""

    # Handle integers
    if isinstance(a, int) and isinstance(b, int):
        if a != b:
            return False, f"{path}: int mismatch (cpp={a}, rust={b})"
        return True, ""

    # Handle strings
    if isinstance(a, str) and isinstance(b, str):
        if a != b:
            return False, f"{path}: string mismatch (cpp={a!r}, rust={b!r})"
        return True, ""

    # Handle booleans
    if isinstance(a, bool) and isinstance(b, bool):
        if a != b:
            return False, f"{path}: bool mismatch (cpp={a}, rust={b})"
        return True, ""

    # Handle lists/arrays
    if isinstance(a, (list, tuple)) and isinstance(b, (list, tuple)):
        if len(a) != len(b):
            return False, f"{path}: list length mismatch (cpp={len(a)}, rust={len(b)})"
        for i, (va, vb) in enumerate(zip(a, b)):
            ok, msg = compare_values(va, vb, f"{path}[{i}]")
            if not ok:
                return False, msg
        return True, ""

    # Handle dicts (structs)
    if isinstance(a, dict) and isinstance(b, dict):
        keys_a = set(a.keys())
        keys_b = set(b.keys())
        if keys_a != keys_b:
            missing_in_rust = keys_a - keys_b
            extra_in_rust = keys_b - keys_a
            return False, f"{path}: struct keys mismatch (missing={missing_in_rust}, extra={extra_in_rust})"
        for key in keys_a:
            ok, msg = compare_values(a[key], b[key], f"{path}.{key}")
            if not ok:
                return False, msg
        return True, ""

    # Type mismatch
    if type(a) != type(b):
        # Special case: int vs float
        if isinstance(a, (int, float)) and isinstance(b, (int, float)):
            return compare_floats(float(a), float(b)), ""
        return False, f"{path}: type mismatch (cpp={type(a).__name__}, rust={type(b).__name__})"

    # Fallback: direct comparison
    if a != b:
        return False, f"{path}: value mismatch (cpp={a}, rust={b})"
    return True, ""

class DuckDBRunner:
    """Runs DuckDB queries via CLI with JSON output."""

    def __init__(self, duckdb_path: Path, extension_load_cmd: str, test_data_path: Path):
        self.duckdb_path = duckdb_path
        self.extension_load_cmd = extension_load_cmd
        self.test_data_path = test_data_path

    def run_query(self, query: str) -> Any:
        """Run a query and return the JSON result."""
        # Build the full SQL script
        with open(self.test_data_path) as f:
            test_data_sql = f.read()

        full_sql = f"""
{self.extension_load_cmd}
.mode json
{test_data_sql}
{query};
"""
        # Write to temp file
        with tempfile.NamedTemporaryFile(mode='w', suffix='.sql', delete=False) as f:
            f.write(full_sql)
            sql_file = f.name

        try:
            result = subprocess.run(
                [str(self.duckdb_path), '-unsigned', '-init', sql_file, '-c', ''],
                capture_output=True,
                text=True,
                timeout=30
            )

            # Clean up temp file
            Path(sql_file).unlink()

            if result.returncode != 0:
                return f"ERROR: {result.stderr}"

            # Parse JSON output - handle multi-line JSON by finding the full JSON array
            output = result.stdout.strip()

            # Find the start of JSON array (first '[' that's the result)
            json_start = -1
            lines = output.split('\n')
            for i, line in enumerate(lines):
                stripped = line.strip()
                if stripped.startswith('[{'):
                    json_start = i
                    break

            if json_start >= 0:
                # Collect all lines from start to end of JSON
                json_lines = []
                bracket_count = 0
                for line in lines[json_start:]:
                    json_lines.append(line)
                    bracket_count += line.count('[') - line.count(']')
                    if bracket_count == 0 and json_lines:
                        break

                json_str = '\n'.join(json_lines)
                try:
                    data = json.loads(json_str)
                    if data and len(data) == 1:
                        # Single row result
                        row = data[0]
                        if len(row) == 1:
                            # Single column - return just the value
                            return list(row.values())[0]
                        return row
                    return data
                except json.JSONDecodeError:
                    pass

            return f"ERROR: Could not parse output: {output[-500:]}"

        except subprocess.TimeoutExpired:
            return "ERROR: Query timed out"
        except Exception as e:
            return f"ERROR: {e}"

def test_function(cpp_runner: DuckDBRunner, rust_runner: DuckDBRunner,
                  name: str, query: str) -> TestResult:
    """Test a function on both extensions and compare results."""
    cpp_result = cpp_runner.run_query(query)
    rust_result = rust_runner.run_query(query)

    # Check for errors
    if isinstance(cpp_result, str) and cpp_result.startswith("ERROR:"):
        return TestResult(name, False, cpp_result, rust_result, f"C++ error: {cpp_result}")
    if isinstance(rust_result, str) and rust_result.startswith("ERROR:"):
        return TestResult(name, False, cpp_result, rust_result, f"Rust error: {rust_result}")

    # Compare results
    ok, msg = compare_values(cpp_result, rust_result, name)
    return TestResult(name, ok, cpp_result, rust_result, msg if not ok else None)

def main():
    script_dir = Path(__file__).parent
    project_dir = script_dir.parent.parent
    test_data_path = script_dir / "test_data.sql"
    build_duckdb = project_dir / "build" / "duckdb"
    rust_ext_path = project_dir / "build" / "extension" / "anofox_forecast" / "anofox_forecast.duckdb_extension"

    # Check Rust extension exists
    if not rust_ext_path.exists():
        print(f"ERROR: Rust extension not found at {rust_ext_path}")
        print("Please build the extension first: cd build && make -j")
        sys.exit(1)

    # Check DuckDB CLI exists
    if not build_duckdb.exists():
        print(f"ERROR: DuckDB CLI not found at {build_duckdb}")
        sys.exit(1)

    print("=" * 80)
    print("Anofox Forecast Extension Comparison Test")
    print("Comparing: C++ Community Extension vs Rust Port")
    print("=" * 80)

    # Create runners for each extension
    print("\n[1/4] Setting up C++ community extension...")
    cpp_runner = DuckDBRunner(
        build_duckdb,
        "INSTALL anofox_forecast FROM community; LOAD anofox_forecast;",
        test_data_path
    )
    print("      C++ extension configured")

    print("\n[2/4] Setting up Rust port extension...")
    rust_runner = DuckDBRunner(
        build_duckdb,
        f"LOAD '{rust_ext_path}';",
        test_data_path
    )
    print("      Rust extension configured")

    # Define all test cases
    test_cases = [
        # Category 1: Metric Functions
        ("ts_mae", "SELECT ts_mae(actual, forecast) FROM test_metric_pairs"),
        ("ts_mse", "SELECT ts_mse(actual, forecast) FROM test_metric_pairs"),
        ("ts_rmse", "SELECT ts_rmse(actual, forecast) FROM test_metric_pairs"),
        ("ts_mape", "SELECT ts_mape(actual, forecast) FROM test_metric_pairs"),
        ("ts_smape", "SELECT ts_smape(actual, forecast) FROM test_metric_pairs"),
        ("ts_mase (period=1)", "SELECT ts_mase(actual, forecast, 1) FROM test_metric_pairs"),
        ("ts_mase (period=7)", "SELECT ts_mase(actual, forecast, 7) FROM test_metric_pairs"),
        ("ts_r2", "SELECT ts_r2(actual, forecast) FROM test_metric_pairs"),
        ("ts_bias", "SELECT ts_bias(actual, forecast) FROM test_metric_pairs"),
        ("ts_rmae", "SELECT ts_rmae(actual, forecast) FROM test_metric_pairs"),
        ("ts_quantile_loss (q=0.1)", "SELECT ts_quantile_loss(actual, forecast, 0.1) FROM test_metric_pairs"),
        ("ts_quantile_loss (q=0.5)", "SELECT ts_quantile_loss(actual, forecast, 0.5) FROM test_metric_pairs"),
        ("ts_quantile_loss (q=0.9)", "SELECT ts_quantile_loss(actual, forecast, 0.9) FROM test_metric_pairs"),
        ("ts_mqloss", "SELECT ts_mqloss(actual, forecast, 0.5) FROM test_metric_pairs"),
        ("ts_coverage", "SELECT ts_coverage(actual, lower_bound, upper_bound) FROM test_metric_pairs"),

        # Category 2: Statistical Functions
        ("ts_stats (random)", "SELECT ts_stats(values) FROM test_random_series"),
        ("ts_stats (trending)", "SELECT ts_stats(values) FROM test_trending_series"),
        ("ts_stats (seasonal)", "SELECT ts_stats(values) FROM test_seasonal_series"),
        ("ts_stats (constant)", "SELECT ts_stats(values) FROM test_constant_series"),
        ("ts_data_quality (random)", "SELECT ts_data_quality(values) FROM test_random_series"),
        ("ts_data_quality (nulls)", "SELECT ts_data_quality(values) FROM test_nulls_series"),

        # Category 3: Filtering Functions
        ("ts_diff (order=1)", "SELECT ts_diff(values, 1) FROM test_random_series"),
        ("ts_diff (order=2)", "SELECT ts_diff(values, 2) FROM test_random_series"),
        # Note: ts_is_constant not in community extension, only ts_drop_constant
        ("ts_drop_constant (constant)", "SELECT ts_drop_constant(values) FROM test_constant_series"),
        ("ts_drop_constant (random)", "SELECT ts_drop_constant(values) FROM test_random_series"),
        ("ts_drop_leading_zeros", "SELECT ts_drop_leading_zeros(values) FROM test_leading_zeros_series"),
        ("ts_drop_trailing_zeros", "SELECT ts_drop_trailing_zeros(values) FROM test_trailing_zeros_series"),
        ("ts_drop_edge_zeros", "SELECT ts_drop_edge_zeros(values) FROM test_edge_zeros_series"),

        # Category 4: Imputation Functions
        ("ts_fill_nulls_const (0)", "SELECT ts_fill_nulls_const(values, 0.0) FROM test_nulls_series"),
        ("ts_fill_nulls_const (99)", "SELECT ts_fill_nulls_const(values, 99.0) FROM test_nulls_series"),
        ("ts_fill_nulls_forward", "SELECT ts_fill_nulls_forward(values) FROM test_nulls_series"),
        ("ts_fill_nulls_backward", "SELECT ts_fill_nulls_backward(values) FROM test_nulls_series"),
        ("ts_fill_nulls_mean", "SELECT ts_fill_nulls_mean(values) FROM test_nulls_series"),

        # Category 5: Seasonality Functions
        ("ts_detect_seasonality", "SELECT ts_detect_seasonality(values) FROM test_seasonal_series"),
        ("ts_analyze_seasonality", "SELECT ts_analyze_seasonality(values) FROM test_seasonal_series"),

        # Category 6: Decomposition Functions
        # Note: Community extension only has single-arg version (auto-detects period)
        ("ts_mstl_decomposition", "SELECT ts_mstl_decomposition(values) FROM test_decomposition_series"),

        # Category 7: Changepoint Functions
        ("ts_detect_changepoints (default)", "SELECT ts_detect_changepoints(values) FROM test_changepoint_series"),
        ("ts_detect_changepoints (min_size=5)", "SELECT ts_detect_changepoints(values, 5, 0.0) FROM test_changepoint_series"),

        # Category 8: Feature Functions
        ("ts_features_list", "SELECT ts_features_list()"),
        ("ts_features (random)", "SELECT ts_features(values) FROM test_random_series"),
        ("ts_features (trending)", "SELECT ts_features(values) FROM test_trending_series"),
        ("ts_features (seasonal)", "SELECT ts_features(values) FROM test_seasonal_series"),

        # Category 9: Forecast Functions
        ("ts_forecast (h=5, auto)", "SELECT ts_forecast(values, 5) FROM test_random_series"),
        ("ts_forecast (h=10, auto)", "SELECT ts_forecast(values, 10) FROM test_trending_series"),
        ("ts_forecast (h=7, naive)", "SELECT ts_forecast(values, 7, 'naive') FROM test_random_series"),
        ("ts_forecast (h=7, snaive)", "SELECT ts_forecast(values, 7, 'snaive') FROM test_seasonal_series"),
        ("ts_forecast (h=12, ets)", "SELECT ts_forecast(values, 12, 'ets') FROM test_trending_series"),

        # Aggregate functions (_agg variants)
        ("ts_stats_agg", """
            SELECT ts_stats(LIST(value_col ORDER BY date_col))
            FROM test_grouped_data WHERE group_id = 'A'
        """),
        ("ts_data_quality_agg", """
            SELECT ts_data_quality(LIST(value_col ORDER BY date_col))
            FROM test_grouped_data WHERE group_id = 'A'
        """),
        ("ts_detect_changepoints_agg", """
            SELECT ts_detect_changepoints(LIST(value_col ORDER BY date_col))
            FROM test_grouped_data WHERE group_id = 'B'
        """),
        ("ts_forecast_agg", """
            SELECT ts_forecast(LIST(value_col ORDER BY date_col), 5)
            FROM test_grouped_data WHERE group_id = 'A'
        """),

        # _by variants (grouped operations)
        ("ts_stats_by", """
            SELECT group_id, ts_stats(LIST(value_col ORDER BY date_col)) as stats
            FROM test_grouped_data
            GROUP BY group_id
            ORDER BY group_id
        """),
        ("ts_forecast_by", """
            SELECT group_id, ts_forecast(LIST(value_col ORDER BY date_col), 5) as forecast
            FROM test_grouped_data
            GROUP BY group_id
            ORDER BY group_id
        """),
    ]

    print(f"\n[3/4] Running {len(test_cases)} comparison tests...")
    print("-" * 80)

    results = []
    passed = 0
    failed = 0

    for name, query in test_cases:
        result = test_function(cpp_runner, rust_runner, name, query)
        results.append(result)

        if result.passed:
            passed += 1
            print(f"  ✓ {name}")
        else:
            failed += 1
            print(f"  ✗ {name}")
            print(f"    Error: {result.error_message}")
            if not isinstance(result.cpp_result, str) or not result.cpp_result.startswith("ERROR:"):
                cpp_str = str(result.cpp_result)[:300]
                print(f"    C++:  {cpp_str}")
            if not isinstance(result.rust_result, str) or not result.rust_result.startswith("ERROR:"):
                rust_str = str(result.rust_result)[:300]
                print(f"    Rust: {rust_str}")

    print("-" * 80)

    # Summary
    print(f"\n[4/4] Test Summary")
    print("=" * 80)
    print(f"  Total tests:  {len(results)}")
    print(f"  Passed:       {passed}")
    print(f"  Failed:       {failed}")
    print(f"  Pass rate:    {100 * passed / len(results):.1f}%")
    print("=" * 80)

    # Generate detailed report
    report_path = script_dir / "RESULTS.md"
    with open(report_path, "w") as f:
        f.write("# Function Comparison Results\n\n")
        f.write(f"**Date:** {__import__('datetime').datetime.now().isoformat()}\n\n")
        f.write(f"**Total Tests:** {len(results)}\n")
        f.write(f"**Passed:** {passed}\n")
        f.write(f"**Failed:** {failed}\n")
        f.write(f"**Pass Rate:** {100 * passed / len(results):.1f}%\n\n")

        if failed > 0:
            f.write("## Failed Tests\n\n")
            for r in results:
                if not r.passed:
                    f.write(f"### {r.function_name}\n\n")
                    f.write(f"**Error:** {r.error_message}\n\n")
                    f.write(f"**C++ Result:**\n```\n{r.cpp_result}\n```\n\n")
                    f.write(f"**Rust Result:**\n```\n{r.rust_result}\n```\n\n")

        f.write("## All Tests\n\n")
        f.write("| Function | Status |\n")
        f.write("|----------|--------|\n")
        for r in results:
            status = "✓ Pass" if r.passed else "✗ Fail"
            f.write(f"| {r.function_name} | {status} |\n")

    print(f"\nDetailed report written to: {report_path}")

    # Exit with error code if any tests failed
    sys.exit(0 if failed == 0 else 1)

if __name__ == "__main__":
    main()
