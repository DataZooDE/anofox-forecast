"""
DuckDB 2.0 vs v1.5.5 comparison driver for anofox_forecast.

Compares a locally-built anofox_forecast extension against DuckDB v2.0-cyanoptera
with the v1.5.5 baseline: parity of forecast output (bitwise-identical values) and
timing of the SQL micro-benchmark / M4 Daily family benchmarks / 10k synthetic test.

Every subprocess started by this driver gets DATAZOO_DISABLE_TELEMETRY=1 forced into
its environment, so a dev-build LOAD never emits PostHog telemetry and timings exclude
network latency.

Usage:
    uv run python duckdb2_compare.py tracer --base-ext <path> --new-ext <path>
"""
import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

# Insert the benchmark root into sys.path the same way the m4 run.py wrappers do,
# so `from src.common...` and `from configs...` resolve regardless of cwd.
sys.path.insert(0, str(Path(__file__).resolve().parent))

import pandas as pd  # noqa: E402

BENCH_ROOT = Path(__file__).resolve().parent
M4_DAILY_PARQUET = BENCH_ROOT / 'data' / 'm4_daily_train_long.parquet'


def _forced_env() -> dict:
    """Base environment for every subprocess this driver starts: telemetry off."""
    env = os.environ.copy()
    env['DATAZOO_DISABLE_TELEMETRY'] = '1'
    return env


def _duckdb_cli_for_ext(ext_path: Path) -> Path:
    """Derive <tree>/build/release/duckdb from an extension path, matching
    _find_duckdb_cli's convention: extension/anofox_forecast/x -> ../../../duckdb."""
    cli = ext_path.parent.parent.parent / 'duckdb'
    if not cli.exists():
        raise FileNotFoundError(
            f"Could not find DuckDB CLI derived from extension path {ext_path} "
            f"(looked for {cli}). Expected layout: <tree>/build/release/extension/"
            f"anofox_forecast/anofox_forecast.duckdb_extension"
        )
    return cli


def cmd_tracer(args: argparse.Namespace) -> int:
    base_ext = Path(args.base_ext).resolve()
    new_ext = Path(args.new_ext).resolve()

    base_cli = _duckdb_cli_for_ext(base_ext)
    new_cli = _duckdb_cli_for_ext(new_ext)

    if not M4_DAILY_PARQUET.exists():
        print(f"ERROR: M4 Daily parquet not found at {M4_DAILY_PARQUET}", file=sys.stderr)
        return 1

    results = {}
    versions = {}
    env = _forced_env()

    for label, ext_path, cli_path in (('v155', base_ext, base_cli), ('v20', new_ext, new_cli)):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmpdir = Path(tmpdir)
            forecast_parquet = tmpdir / 'forecast.parquet'
            version_parquet = tmpdir / 'version.parquet'

            sql = f"""
LOAD '{ext_path}';
CREATE TABLE s AS
    SELECT * FROM read_parquet('{M4_DAILY_PARQUET}')
    WHERE unique_id IN (
        SELECT DISTINCT unique_id FROM read_parquet('{M4_DAILY_PARQUET}')
        ORDER BY unique_id LIMIT 20
    );
COPY (
    SELECT * FROM ts_forecast_by('s', unique_id, ds, y, 'AutoETS', 14, '1d')
    ORDER BY unique_id, forecast_step, ds, yhat, yhat_lower, yhat_upper, model_name
) TO '{forecast_parquet}' (FORMAT PARQUET);
COPY (SELECT library_version, source_id FROM pragma_version())
    TO '{version_parquet}' (FORMAT PARQUET);
"""
            script_file = tmpdir / 'tracer.sql'
            script_file.write_text(sql)

            proc = subprocess.run(
                [str(cli_path), '-unsigned', '-c', f".read '{script_file}'"],
                capture_output=True, text=True, env=env, timeout=300,
            )
            if proc.returncode != 0:
                print(f"ERROR: {label} CLI failed (exit {proc.returncode})", file=sys.stderr)
                print(f"STDOUT: {proc.stdout}\nSTDERR: {proc.stderr}", file=sys.stderr)
                return 1
            if not forecast_parquet.exists() or not version_parquet.exists():
                print(f"ERROR: {label} CLI produced no output parquet.\n"
                      f"STDOUT: {proc.stdout}\nSTDERR: {proc.stderr}", file=sys.stderr)
                return 1

            results[label] = pd.read_parquet(forecast_parquet)
            ver = pd.read_parquet(version_parquet).iloc[0]
            versions[label] = (ver['library_version'], ver['source_id'])

    print(f"v1.5.5: library_version={versions['v155'][0]} source_id={versions['v155'][1]}")
    print(f"v2.0:   library_version={versions['v20'][0]} source_id={versions['v20'][1]}")

    df_base = results['v155'].sort_values(
        ['unique_id', 'forecast_step', 'ds']).reset_index(drop=True)
    df_new = results['v20'].sort_values(
        ['unique_id', 'forecast_step', 'ds']).reset_index(drop=True)

    if list(df_base.columns) != list(df_new.columns):
        print(f"TRACER PARITY MISMATCH: column sets differ.\n"
              f"v1.5.5: {list(df_base.columns)}\nv2.0:   {list(df_new.columns)}", file=sys.stderr)
        return 1
    if len(df_base) != len(df_new):
        print(f"TRACER PARITY MISMATCH: row counts differ "
              f"(v1.5.5={len(df_base)}, v2.0={len(df_new)})", file=sys.stderr)
        return 1

    try:
        pd.testing.assert_frame_equal(df_base, df_new, check_exact=True)
    except AssertionError as e:
        print("TRACER PARITY MISMATCH:", file=sys.stderr)
        print(str(e), file=sys.stderr)
        return 1

    print(f"TRACER PARITY OK rows={len(df_base)}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)

    p_tracer = sub.add_parser('tracer', help='Forecast-parity smoke test on a 20-series subset')
    p_tracer.add_argument('--base-ext', required=True, help='Path to the v1.5.5 extension')
    p_tracer.add_argument('--new-ext', required=True, help='Path to the v2.0 extension')
    p_tracer.set_defaults(func=cmd_tracer)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return args.func(args)


if __name__ == '__main__':
    sys.exit(main())
