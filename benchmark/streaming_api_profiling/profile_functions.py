#!/usr/bin/env python3
"""
Memory and CPU Profiling Framework for Table Macros

This script profiles table macro functions to measure CPU time and memory usage,
helping identify candidates for streaming API conversion.

Usage:
    python benchmark/streaming_api_profiling/profile_functions.py [--quick] [--function FUNC]

Options:
    --quick      Use smaller dataset (100K rows instead of 1M)
    --function   Profile only specific function(s), comma-separated
    --output     Output file for results (default: profile_results.json)
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile
from dataclasses import dataclass, asdict
from datetime import datetime
from pathlib import Path
from typing import Optional


@dataclass
class ProfileResult:
    """Result of profiling a single function."""
    function_name: str
    dataset: str
    rows: int
    groups: int
    latency_ms: float
    memory_peak_mb: float
    temp_dir_mb: float
    total_memory_mb: float
    status: str
    error: Optional[str] = None
    query: Optional[str] = None


# Function definitions with their profiling queries
# Each entry: (function_name, query_template, requires_native_comparison)
FUNCTIONS_TO_PROFILE = {
    # High Priority - CV/Forecasting
    "ts_cv_forecast_by": {
        "query": """
            WITH cv_splits AS (
                SELECT * FROM ts_cv_split_by(
                    '{table}', series_id, date, value,
                    ['2023-03-01'::DATE, '2023-03-15'::DATE],
                    7, '1d', MAP{{}}
                )
            )
            SELECT * FROM ts_cv_forecast_by(
                'cv_splits', series_id, date, value,
                'Naive', 7, MAP{{}}, '1d'
            )
        """,
        "priority": "P1",
        "category": "cv_forecasting"
    },
    "ts_cv_split_by": {
        "query": """
            SELECT * FROM ts_cv_split_by(
                '{table}', series_id, date, value,
                ['2023-03-01'::DATE, '2023-03-15'::DATE],
                7, '1d', MAP{{}}
            )
        """,
        "priority": "P1",
        "category": "cv_forecasting"
    },

    # Medium Priority - Forecasting
    "ts_forecast_by": {
        "query": """
            SELECT * FROM ts_forecast_by(
                '{table}', series_id, date, value,
                'Naive', 7, MAP{{}}
            )
        """,
        "priority": "P2",
        "category": "forecasting"
    },

    # Medium Priority - Statistics
    "ts_stats_by": {
        "query": """
            SELECT * FROM ts_stats_by('{table}', series_id, date, value, '1d')
        """,
        "priority": "P2",
        "category": "statistics"
    },

    # Medium Priority - Features
    "ts_features_by": {
        "query": """
            SELECT * FROM ts_features_by('{table}', series_id, date, value)
        """,
        "priority": "P2",
        "category": "features"
    },

    # Medium Priority - Decomposition
    "ts_mstl_decomposition_by": {
        "query": """
            SELECT * FROM ts_mstl_decomposition_by(
                '{table}', series_id, date, value, [7]
            )
        """,
        "priority": "P2",
        "category": "decomposition"
    },

    # Medium Priority - Conformal (requires backtest results as input)
    "ts_conformal_by": {
        "query": """
            SELECT * FROM ts_conformal_by(
                '{table}', series_id, date, value,
                7, 5, '1d',
                MAP{{'method': 'Naive', 'alpha': 0.1}}
            )
        """,
        "priority": "P2",
        "category": "conformal",
        "skip": True  # Requires backtest results table format
    },
    "ts_conformal_apply_by": {
        "query": """
            SELECT 1
        """,
        "priority": "P2",
        "category": "conformal",
        "skip": True  # Needs calibration data
    },
    "ts_conformal_calibrate": {
        "query": """
            SELECT 1
        """,
        "priority": "P2",
        "category": "conformal",
        "skip": True  # Scalar function, not table macro
    },

    # Medium Priority - Data Quality
    "ts_data_quality_by": {
        "query": """
            SELECT * FROM ts_data_quality_by('{table}', series_id, date, value, 10, '1d')
        """,
        "priority": "P2",
        "category": "data_quality"
    },
    "ts_data_quality": {
        "query": """
            SELECT * FROM ts_data_quality('{table}', date, value, 10, '1d')
        """,
        "priority": "P2",
        "category": "data_quality"
    },
    "ts_data_quality_summary": {
        "query": """
            SELECT * FROM ts_data_quality_summary('{table}', series_id, date, value, 10, '1d')
        """,
        "priority": "P2",
        "category": "data_quality"
    },
    "ts_quality_report": {
        "query": """
            SELECT * FROM ts_quality_report('{table}', series_id, date, value, 10, '1d')
        """,
        "priority": "P2",
        "category": "data_quality"
    },

    # Lower Priority - Detection
    "ts_detect_periods_by": {
        "query": """
            SELECT * FROM ts_detect_periods_by('{table}', series_id, date, value, MAP{{}})
        """,
        "priority": "P3",
        "category": "detection"
    },
    "ts_classify_seasonality_by": {
        "query": """
            SELECT * FROM ts_classify_seasonality_by('{table}', series_id, date, value, 7)
        """,
        "priority": "P3",
        "category": "detection"
    },
    "ts_detect_changepoints_by": {
        "query": """
            SELECT * FROM ts_detect_changepoints_by('{table}', series_id, date, value, MAP{{}})
        """,
        "priority": "P3",
        "category": "detection"
    },

    # GH#113 - Fill functions (for comparison with native)
    "ts_fill_gaps_by": {
        "query": """
            SELECT * FROM ts_fill_gaps_by('{table}', series_id, date, value, '1d')
        """,
        "priority": "P1",
        "category": "fill",
        "has_native": True
    },
    "ts_fill_gaps_native": {
        "query": """
            SELECT * FROM ts_fill_gaps_native(
                (SELECT series_id, date, value FROM {table}),
                '1d'
            )
        """,
        "priority": "P1",
        "category": "fill",
        "is_native": True
    },
    "ts_fill_forward_by": {
        "query": """
            SELECT * FROM ts_fill_forward_by(
                '{table}', series_id, date, value,
                '2023-04-30'::DATE, '1d'
            )
        """,
        "priority": "P1",
        "category": "fill",
        "has_native": True
    },
    "ts_fill_forward_native": {
        "query": """
            SELECT * FROM ts_fill_forward_native(
                (SELECT series_id, date, value FROM {table}),
                '2023-04-30'::DATE,
                '1d'
            )
        """,
        "priority": "P1",
        "category": "fill",
        "is_native": True
    },
}


def get_duckdb_path() -> str:
    """Get path to DuckDB executable."""
    # Try to find in build directory
    candidates = [
        "build/release/duckdb",
        "build/debug/duckdb",
        "duckdb",
    ]
    for candidate in candidates:
        if os.path.exists(candidate):
            return candidate
    raise FileNotFoundError("DuckDB executable not found")


def run_profile_query(
    duckdb_path: str,
    extension_path: str,
    function_name: str,
    query: str,
    table: str,
    profile_output: str,
    db_path: str
) -> ProfileResult:
    """Run a single profiling query and return results."""

    # Format query with table name
    formatted_query = query.format(table=table)

    # Build the complete SQL script
    # NOTE: We wrap the query in SELECT COUNT(*) FROM (...) to work around a DuckDB bug
    # where table_in_out functions fail with BatchedDataCollection::Merge errors when
    # returning all rows with profiling enabled. COUNT(*) still executes the full query
    # so memory/CPU measurements are accurate.
    sql_script = f"""
-- Load extension
LOAD '{extension_path}';

-- Configure profiling
PRAGMA enable_profiling = 'json';
PRAGMA profiling_output = '{profile_output}';

-- Run the query wrapped in COUNT(*) to avoid BatchedDataCollection bug with profiling
SELECT COUNT(*) FROM ({formatted_query}) _profiled_query;

-- Disable profiling
PRAGMA disable_profiling;

-- Read and output profile metrics
SELECT
    latency,
    COALESCE(system_peak_buffer_memory, 0) AS memory_bytes,
    COALESCE(system_peak_temp_dir_size, 0) AS temp_bytes
FROM '{profile_output}';
"""

    try:
        # NOTE: Using -csv alone (not -csv -noheader) to work around DuckDB bug
        # where combining -csv -noheader with profiling causes BatchedDataCollection
        # errors for table_in_out functions. We parse the output to skip headers.
        result = subprocess.run(
            [duckdb_path, db_path, "-csv"],
            input=sql_script,
            capture_output=True,
            text=True,
            timeout=300  # 5 minute timeout
        )

        if result.returncode != 0:
            return ProfileResult(
                function_name=function_name,
                dataset=table,
                rows=0,
                groups=0,
                latency_ms=0,
                memory_peak_mb=0,
                temp_dir_mb=0,
                total_memory_mb=0,
                status="error",
                error=result.stderr[:500] if result.stderr else "Unknown error",
                query=formatted_query
            )

        # Parse output (CSV format with header: latency,memory_bytes,temp_bytes)
        lines = result.stdout.strip().split('\n')
        if len(lines) < 2:  # Need at least header + data
            return ProfileResult(
                function_name=function_name,
                dataset=table,
                rows=0,
                groups=0,
                latency_ms=0,
                memory_peak_mb=0,
                temp_dir_mb=0,
                total_memory_mb=0,
                status="error",
                error="No output from profiling query",
                query=formatted_query
            )

        # Get the last line (the metrics), skip header
        parts = lines[-1].split(',')
        latency_ms = float(parts[0]) if len(parts) > 0 else 0
        memory_bytes = float(parts[1]) if len(parts) > 1 else 0
        temp_bytes = float(parts[2]) if len(parts) > 2 else 0

        memory_mb = memory_bytes / (1024 * 1024)
        temp_mb = temp_bytes / (1024 * 1024)

        # Get row count for the table (no profiling, can use -noheader)
        row_count_result = subprocess.run(
            [duckdb_path, db_path, "-csv", "-noheader"],
            input=f"LOAD '{extension_path}'; SELECT COUNT(*), COUNT(DISTINCT series_id) FROM {table};",
            capture_output=True,
            text=True
        )
        row_parts = row_count_result.stdout.strip().split(',')
        rows = int(row_parts[0]) if row_parts and row_parts[0].isdigit() else 0
        groups = int(row_parts[1]) if len(row_parts) > 1 and row_parts[1].isdigit() else 0

        return ProfileResult(
            function_name=function_name,
            dataset=table,
            rows=rows,
            groups=groups,
            latency_ms=latency_ms,
            memory_peak_mb=round(memory_mb, 2),
            temp_dir_mb=round(temp_mb, 2),
            total_memory_mb=round(memory_mb + temp_mb, 2),
            status="success",
            query=formatted_query
        )

    except subprocess.TimeoutExpired:
        return ProfileResult(
            function_name=function_name,
            dataset=table,
            rows=0,
            groups=0,
            latency_ms=0,
            memory_peak_mb=0,
            temp_dir_mb=0,
            total_memory_mb=0,
            status="timeout",
            error="Query timed out after 5 minutes",
            query=formatted_query
        )
    except Exception as e:
        return ProfileResult(
            function_name=function_name,
            dataset=table,
            rows=0,
            groups=0,
            latency_ms=0,
            memory_peak_mb=0,
            temp_dir_mb=0,
            total_memory_mb=0,
            status="error",
            error=str(e),
            query=formatted_query
        )


def generate_test_data(duckdb_path: str, extension_path: str, db_path: str) -> bool:
    """Generate test data if not already present."""
    script_dir = Path(__file__).parent
    generate_script = script_dir / "generate_test_data.sql"

    # Check if data already exists
    check_result = subprocess.run(
        [duckdb_path, db_path, "-csv", "-noheader"],
        input="SELECT COUNT(*) FROM profile_test_data;",
        capture_output=True,
        text=True
    )
    if check_result.returncode == 0 and check_result.stdout.strip().isdigit():
        count = int(check_result.stdout.strip())
        if count > 0:
            print(f"Test data already exists ({count:,} rows)")
            return True

    print("Generating test data...")
    result = subprocess.run(
        [duckdb_path, db_path],
        input=f"LOAD '{extension_path}';\n" + generate_script.read_text(),
        capture_output=True,
        text=True
    )

    if result.returncode != 0:
        print(f"Error generating test data: {result.stderr}")
        return False

    print("Test data generated successfully")
    return True


def format_results_table(results: list[ProfileResult]) -> str:
    """Format results as a markdown table."""
    lines = [
        "| Function | Rows | Groups | Latency (ms) | Memory (MB) | Temp (MB) | Total (MB) | Status |",
        "|----------|------|--------|--------------|-------------|-----------|------------|--------|"
    ]

    for r in sorted(results, key=lambda x: (x.status != "success", -x.total_memory_mb)):
        lines.append(
            f"| {r.function_name} | {r.rows:,} | {r.groups:,} | {r.latency_ms:.1f} | "
            f"{r.memory_peak_mb:.1f} | {r.temp_dir_mb:.1f} | {r.total_memory_mb:.1f} | {r.status} |"
        )

    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Profile table macro functions")
    parser.add_argument("--quick", action="store_true", help="Use smaller dataset")
    parser.add_argument("--function", type=str, help="Profile only specific function(s), comma-separated")
    parser.add_argument("--output", type=str, default="profile_results.json", help="Output file")
    parser.add_argument("--category", type=str, help="Profile only functions in category")
    args = parser.parse_args()

    # Setup paths
    duckdb_path = get_duckdb_path()
    extension_path = "build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension"
    script_dir = Path(__file__).parent
    db_path = str(script_dir / "profile_benchmark.duckdb")

    if not os.path.exists(extension_path):
        print(f"Extension not found at {extension_path}")
        print("Run: make release")
        sys.exit(1)

    # Generate test data
    if not generate_test_data(duckdb_path, extension_path, db_path):
        sys.exit(1)

    # Select table based on --quick flag
    table = "profile_test_data_small" if args.quick else "profile_test_data"

    # Filter functions if specified
    functions = FUNCTIONS_TO_PROFILE.copy()
    if args.function:
        requested = set(args.function.split(","))
        functions = {k: v for k, v in functions.items() if k in requested}
    if args.category:
        functions = {k: v for k, v in functions.items() if v.get("category") == args.category}

    # Skip functions marked as skip
    functions = {k: v for k, v in functions.items() if not v.get("skip")}

    print(f"\nProfiling {len(functions)} functions using {table}...")
    print("-" * 60)

    results = []
    with tempfile.TemporaryDirectory() as tmpdir:
        for func_name, func_info in functions.items():
            print(f"  {func_name}...", end=" ", flush=True)

            profile_output = os.path.join(tmpdir, f"{func_name}_profile.json")
            result = run_profile_query(
                duckdb_path=duckdb_path,
                extension_path=extension_path,
                function_name=func_name,
                query=func_info["query"],
                table=table,
                profile_output=profile_output,
                db_path=db_path
            )
            results.append(result)

            if result.status == "success":
                print(f"{result.latency_ms:.0f}ms, {result.total_memory_mb:.1f}MB")
            else:
                print(f"{result.status}: {result.error[:50] if result.error else 'unknown'}")

    # Output results
    print("\n" + "=" * 60)
    print("RESULTS SUMMARY")
    print("=" * 60)
    print(format_results_table(results))

    # Save to JSON
    output_data = {
        "timestamp": datetime.now().isoformat(),
        "dataset": table,
        "results": [asdict(r) for r in results]
    }
    with open(args.output, "w") as f:
        json.dump(output_data, f, indent=2)
    print(f"\nResults saved to {args.output}")

    # Print comparison for fill functions (GH#113)
    fill_results = [r for r in results if "fill" in r.function_name.lower()]
    if len(fill_results) >= 2:
        print("\n" + "=" * 60)
        print("FILL FUNCTION COMPARISON (GH#113)")
        print("=" * 60)
        print(format_results_table(fill_results))


if __name__ == "__main__":
    main()
