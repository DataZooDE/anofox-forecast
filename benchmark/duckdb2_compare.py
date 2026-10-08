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
    uv run python duckdb2_compare.py m4 --label v155 --ext <path> --families baseline,ets,theta,arima,mfles,mstl
    uv run python duckdb2_compare.py sqlbench --label v155a --ext <path> --reps 3
    uv run python duckdb2_compare.py synth10k --label v155 --root <tree-root>
    uv run python duckdb2_compare.py compare
"""
import argparse
import hashlib
import importlib
import json
import os
import re
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

# Insert the benchmark root into sys.path the same way the m4 run.py wrappers do,
# so `from src.common...` and `from configs...` resolve regardless of cwd.
sys.path.insert(0, str(Path(__file__).resolve().parent))

import pandas as pd  # noqa: E402

from src.common.data import get_data  # noqa: E402
from src.common.anofox_runner import run_anofox_benchmark  # noqa: E402
from src.common.evaluation import evaluate_forecasts  # noqa: E402

BENCH_ROOT = Path(__file__).resolve().parent
MAIN_ROOT = BENCH_ROOT.parent
M4_DAILY_PARQUET = BENCH_ROOT / 'data' / 'm4_daily_train_long.parquet'
RESULTS_ROOT = BENCH_ROOT / 'results' / 'duckdb2_compare'
EVID_DIR = (MAIN_ROOT / '.planning' / 'quick' /
            '261006-ujg-test-anofox-forecast-against-duckdb-2-0-' / 'evidence')

ALL_M4_FAMILIES = ['baseline', 'ets', 'theta', 'arima', 'mfles', 'mstl']
EXPECTED_MODEL_COUNT = 19  # 5 + 7 + 4 + 1 + 1 + 1 across the 6 families above

SQLBENCH_FILE = BENCH_ROOT / 'sql' / 'duckdb2_compare.sql'
SYNTH10K_FILE = BENCH_ROOT / 'sql' / '10k_series_synthetic_test.sql'


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


def cmd_m4(args: argparse.Namespace) -> int:
    ext_path = Path(args.ext).resolve()
    families = [f.strip() for f in args.families.split(',') if f.strip()]
    unknown = [f for f in families if f not in ALL_M4_FAMILIES]
    if unknown:
        print(f"ERROR: unknown M4 family/families: {unknown}. Known: {ALL_M4_FAMILIES}",
              file=sys.stderr)
        return 1

    # Per-series TS_FORECAST_BY must go through the build's own CLI: the venv's
    # `duckdb` Python package can only match one engine version, and we're comparing two.
    os.environ['ANOFOX_USE_CLI'] = '1'
    os.environ['ANOFOX_CLI_TIMEOUT'] = '7200'
    os.environ['DATAZOO_DISABLE_TELEMETRY'] = '1'

    total_models = 0
    for family in families:
        cfg = importlib.import_module(f'configs.{family}')
        # get_data() must be called fresh per family: run_anofox_benchmark mutates its
        # train_df['ds'] column in place (M4's native integer-index ds -> real dates ->
        # .dt.date), which is harmless when each family runs in its own process (the
        # production m4/*/run.py wrappers) but corrupts a SHARED train_df across
        # multiple families in the same process (2nd+ family sees already-mutated
        # ds and crashes trying to re-convert date objects via .astype(int)).
        family_train_df, horizon, freq, seasonality = get_data('m4', 'Daily', train=True)
        test_df, _, _, _ = get_data('m4', 'Daily', train=False)
        max_series = getattr(cfg, 'MAX_SERIES', 0)
        if max_series and max_series > 0:
            all_ids = family_train_df['unique_id'].unique()
            if len(all_ids) > max_series:
                selected_ids = sorted(all_ids)[:max_series]
                family_train_df = family_train_df[
                    family_train_df['unique_id'].isin(selected_ids)].copy()

        fn_name = getattr(cfg, 'FUNCTION_NAME', 'TS_FORECAST_BY')
        output_dir = RESULTS_ROOT / args.label / family
        print(f"\n### M4 family={family} label={args.label} models={len(cfg.MODELS)} ###")

        metrics = run_anofox_benchmark(
            benchmark_name=cfg.BENCHMARK_NAME,
            train_df=family_train_df,
            horizon=horizon,
            seasonality=seasonality,
            models_config=cfg.MODELS,
            output_dir=output_dir,
            group='Daily',
            freq=freq,
            extension_path=ext_path,
            function_name=fn_name,
        )
        if not metrics:
            print(f"ERROR: family {family} produced no successful model forecasts", file=sys.stderr)
            return 1
        total_models += len(metrics)

        evaluate_forecasts(
            benchmark_name=cfg.BENCHMARK_NAME,
            test_df_pd=test_df,
            train_df_pd=family_train_df,
            seasonality=seasonality,
            results_dir=output_dir,
            group='Daily',
        )

    print(f"\nM4 label={args.label}: {total_models} models produced across {len(families)} families "
          f"(expected {EXPECTED_MODEL_COUNT})")
    if total_models != EXPECTED_MODEL_COUNT and set(families) == set(ALL_M4_FAMILIES):
        print(f"ERROR: expected {EXPECTED_MODEL_COUNT} models across all families, got {total_models}",
              file=sys.stderr)
        return 1
    return 0


def _run_rep(cli_path: Path, cwd: Path, cmd_flag_value: str, stdin_file: Path,
             timeout: int, env: dict) -> subprocess.CompletedProcess:
    return subprocess.run(
        [str(cli_path), '-unsigned', '-cmd', cmd_flag_value],
        stdin=stdin_file.open('r'),
        capture_output=True, text=True, cwd=str(cwd), env=env, timeout=timeout,
    )


_BENCH_RE = re.compile(r'^BENCH (\S+)$')
_RUNTIME_RE = re.compile(r'^Run Time \(s\): real (\d+\.\d+)')


def _parse_sqlbench_output(stdout: str) -> dict:
    """Map each `BENCH <label>` print to its following 'Run Time (s): real X' value
    and a checksum of the printed result rows in between (sanity against output drift
    across reps/builds beyond pure timing)."""
    lines = stdout.splitlines()
    results = {}
    current_label = None
    current_rows = []
    for line in lines:
        m = _BENCH_RE.match(line.strip())
        if m:
            current_label = m.group(1)
            current_rows = []
            continue
        if current_label is None:
            continue
        m2 = _RUNTIME_RE.match(line.strip())
        if m2:
            checksum = hashlib.sha256('\n'.join(current_rows).encode()).hexdigest()[:16]
            results[current_label] = {'seconds': float(m2.group(1)), 'checksum': checksum}
            current_label = None
            current_rows = []
            continue
        current_rows.append(line)
    return results


def cmd_sqlbench(args: argparse.Namespace) -> int:
    ext_path = Path(args.ext).resolve()
    cli_path = _duckdb_cli_for_ext(ext_path)
    if not SQLBENCH_FILE.exists():
        print(f"ERROR: {SQLBENCH_FILE} not found", file=sys.stderr)
        return 1

    env = _forced_env()
    reps_data = []
    for rep in range(args.reps):
        print(f"sqlbench label={args.label} rep={rep + 1}/{args.reps}")
        proc = _run_rep(cli_path, MAIN_ROOT, f"LOAD '{ext_path}'", SQLBENCH_FILE,
                         timeout=1800, env=env)
        if proc.returncode != 0:
            print(f"ERROR: sqlbench rep {rep} failed (exit {proc.returncode})\n"
                  f"STDOUT: {proc.stdout}\nSTDERR: {proc.stderr}", file=sys.stderr)
            return 1
        parsed = _parse_sqlbench_output(proc.stdout)
        expected_labels = {'forecast_naive', 'forecast_autoets', 'forecast_theta', 'forecast_mfles',
                            'stats_by', 'features_by', 'cv_folds', 'cv_forecast', 'fill_gaps'}
        missing = expected_labels - set(parsed.keys())
        if missing:
            print(f"ERROR: sqlbench rep {rep} missing timing for labels: {missing}\n"
                  f"STDOUT tail: {proc.stdout[-2000:]}", file=sys.stderr)
            return 1
        reps_data.append(parsed)

    out_dir = RESULTS_ROOT / 'sqlbench'
    out_dir.mkdir(parents=True, exist_ok=True)
    out_file = out_dir / f'{args.label}.json'
    out_file.write_text(json.dumps({'label': args.label, 'ext': str(ext_path), 'reps': reps_data}, indent=2))
    print(f"Wrote {out_file}")
    return 0


def cmd_synth10k(args: argparse.Namespace) -> int:
    root = Path(args.root).resolve()
    cli_path = root / 'build' / 'release' / 'duckdb'
    if not cli_path.exists():
        print(f"ERROR: DuckDB CLI not found at {cli_path}", file=sys.stderr)
        return 1
    if not SYNTH10K_FILE.exists():
        print(f"ERROR: {SYNTH10K_FILE} not found", file=sys.stderr)
        return 1

    env = _forced_env()
    start = time.time()
    proc = subprocess.run(
        [str(cli_path), '-unsigned', '-cmd', '.timer on'],
        stdin=SYNTH10K_FILE.open('r'),
        capture_output=True, text=True, cwd=str(root), env=env, timeout=3600,
    )
    wall_seconds = time.time() - start

    statement_seconds = [float(m.group(1)) for m in
                          (re.match(r'Run Time \(s\): real (\d+\.\d+)', line.strip())
                           for line in proc.stdout.splitlines()) if m]
    error_count = proc.stderr.count('Error:')

    # The fixture (benchmark/sql/10k_series_synthetic_test.sql) is pre-existing and stale: ~25
    # of its 31 TS_FORECAST_BY calls use the old 7-positional-arg form (params MAP as the 7th
    # arg, no frequency arg before it), which no longer matches ts_forecast_by's current
    # signature (source, group, date, value, method, horizon, frequency, params := MAP{}).
    # This fails identically on v1.5.5 and v2.0 (confirmed: same error, same failing calls on
    # both) and is tracked as a FINDING in the report, not fixed here (out of scope: this task
    # tests the DuckDB 2.0 port, not pre-existing fixture staleness). Tolerate a non-zero exit
    # as long as at least one statement timed successfully, so the timing comparison that IS
    # possible from this fixture still happens; a hard zero means a total failure (e.g. the
    # extension itself didn't load), which is fatal.
    if proc.returncode != 0:
        print(f"WARNING: synth10k exited {proc.returncode} ({error_count} statement errors in "
              f"stderr) — see known fixture staleness note. Continuing with the "
              f"{len(statement_seconds)} statements that did time successfully.", file=sys.stderr)
    if not statement_seconds:
        print(f"ERROR: synth10k produced no 'Run Time (s): real' lines to sum.\n"
              f"STDOUT tail: {proc.stdout[-2000:]}\nSTDERR: {proc.stderr[-2000:]}", file=sys.stderr)
        return 1

    out_dir = RESULTS_ROOT / 'synth10k'
    out_dir.mkdir(parents=True, exist_ok=True)
    out_file = out_dir / f'{args.label}.json'
    out_file.write_text(json.dumps({
        'label': args.label, 'root': str(root),
        'wall_seconds': wall_seconds,
        'summed_statement_seconds': sum(statement_seconds),
        'statement_count': len(statement_seconds),
        'error_count': error_count,
        'cli_returncode': proc.returncode,
    }, indent=2))
    print(f"synth10k label={args.label}: wall={wall_seconds:.2f}s "
          f"summed_statements={sum(statement_seconds):.2f}s ({len(statement_seconds)} statements)")
    print(f"Wrote {out_file}")
    return 0


def _pct_delta(new: float, base: float) -> float:
    if base == 0:
        return float('nan')
    return (new - base) / base * 100.0


def _compare_m4(lines: list) -> bool:
    """Returns True unless one of the 19 expected models is missing for either label
    (the only condition `compare` treats as fatal, per plan). All other mismatches
    (accuracy differences, timing) are recorded inline as FINDING but do not fail
    the command."""
    models_complete = True
    lines.append("# M4 Daily family benchmark comparison\n")
    lines.append("Harness time includes a constant CLI-subprocess start + parquet round-trip "
                  "overhead per model (ANOFOX_USE_CLI=1 path), identical for both labels.\n")
    lines.append("| family | model | v1.5.5 s | 2.0 s | delta % | MASE/MAE/RMSE identical | "
                  "max abs yhat diff | rows differing |")
    lines.append("|---|---|---|---|---|---|---|---|")

    models_seen = {'v155': set(), 'v20': set()}
    for family in ALL_M4_FAMILIES:
        base_dir = RESULTS_ROOT / 'v155' / family
        new_dir = RESULTS_ROOT / 'v20' / family
        base_metrics_files = sorted(base_dir.glob(f'anofox-{family}-*-metrics.parquet')) if base_dir.exists() else []
        new_metrics_files = sorted(new_dir.glob(f'anofox-{family}-*-metrics.parquet')) if new_dir.exists() else []
        if not base_metrics_files or not new_metrics_files:
            lines.append(f"| {family} | (missing) | - | - | - | - | - | - |")
            models_complete = False
            continue
        base_metrics = pd.read_parquet(base_metrics_files[0])
        new_metrics = pd.read_parquet(new_metrics_files[0])
        base_fcst_files = [p for p in sorted(base_dir.glob(f'anofox-{family}-*.parquet'))
                           if not p.name.endswith('-metrics.parquet')]
        new_fcst_files = [p for p in sorted(new_dir.glob(f'anofox-{family}-*.parquet'))
                          if not p.name.endswith('-metrics.parquet')]
        base_fcst = pd.read_parquet(base_fcst_files[0])
        new_fcst = pd.read_parquet(new_fcst_files[0])
        base_eval_files = sorted(base_dir.glob(f'{family}-evaluation-*.parquet'))
        new_eval_files = sorted(new_dir.glob(f'{family}-evaluation-*.parquet'))
        base_eval = pd.read_parquet(base_eval_files[0]) if base_eval_files else None
        new_eval = pd.read_parquet(new_eval_files[0]) if new_eval_files else None

        for _, row in base_metrics.iterrows():
            model_full = row['model']  # e.g. 'anofox-Naive'
            model = model_full.replace('anofox-', '', 1)
            models_seen['v155'].add(model)
            new_row = new_metrics[new_metrics['model'] == model_full]
            if new_row.empty:
                lines.append(f"| {family} | {model} | {row['time_seconds']:.2f} | (missing) | - | - | - | - |")
                models_complete = False
                continue
            new_time = float(new_row.iloc[0]['time_seconds'])
            models_seen['v20'].add(model)
            delta = _pct_delta(new_time, row['time_seconds'])

            max_abs_diff = float('nan')
            rows_differing = -1
            if model in base_fcst.columns and model in new_fcst.columns:
                merged = base_fcst[['unique_id', 'ds', model]].merge(
                    new_fcst[['unique_id', 'ds', model]], on=['unique_id', 'ds'],
                    suffixes=('_v155', '_v20'), how='outer')
                diffs = (merged[f'{model}_v155'] - merged[f'{model}_v20']).abs()
                max_abs_diff = float(diffs.max())
                rows_differing = int((diffs > 1e-9).sum())

            metrics_identical = 'n/a'
            if base_eval is not None and new_eval is not None:
                be = base_eval[base_eval['model'] == model_full]
                ne = new_eval[new_eval['model'] == model_full]
                if not be.empty and not ne.empty:
                    same = all(
                        abs(float(be.iloc[0][c]) - float(ne.iloc[0][c])) < 1e-9
                        for c in ('mase', 'mae', 'rmse') if c in be.columns and c in ne.columns
                    )
                    metrics_identical = 'yes' if same else 'FINDING: differs'

            finding = '' if (rows_differing == 0 or rows_differing == -1) else ' FINDING'
            lines.append(f"| {family} | {model} | {row['time_seconds']:.2f} | {new_time:.2f} | "
                         f"{delta:+.1f} | {metrics_identical} | {max_abs_diff:.6g} | "
                         f"{rows_differing}{finding} |")

    missing_v155 = models_seen['v20'] - models_seen['v155']
    missing_v20 = models_seen['v155'] - models_seen['v20']
    if len(models_seen['v155']) != EXPECTED_MODEL_COUNT or len(models_seen['v20']) != EXPECTED_MODEL_COUNT:
        lines.append(f"\nFINDING: expected {EXPECTED_MODEL_COUNT} models for both labels; "
                     f"got v1.5.5={len(models_seen['v155'])}, v2.0={len(models_seen['v20'])}. "
                     f"missing_from_v155={missing_v155 or '{}'} missing_from_v20={missing_v20 or '{}'}")
        models_complete = False
    return models_complete


def _compare_sqlbench(lines: list) -> bool:
    ok = True
    lines.append("# SQL micro-benchmark comparison\n")
    lines.append("| label | median v1.5.5 s | median 2.0 s | delta % | baseline drift % (run1 vs run2) | checksum match |")
    lines.append("|---|---|---|---|---|---|")

    sqlbench_dir = RESULTS_ROOT / 'sqlbench'
    files = {name: sqlbench_dir / f'{name}.json' for name in ('v155a', 'v20', 'v155b')}
    data = {}
    for name, path in files.items():
        if not path.exists():
            lines.append(f"\nFINDING: missing sqlbench result file {path}")
            ok = False
            return ok
        data[name] = json.loads(path.read_text())

    all_labels = set()
    for d in data.values():
        for rep in d['reps']:
            all_labels.update(rep.keys())

    for label in sorted(all_labels):
        v155a_times = [rep[label]['seconds'] for rep in data['v155a']['reps'] if label in rep]
        v20_times = [rep[label]['seconds'] for rep in data['v20']['reps'] if label in rep]
        v155b_times = [rep[label]['seconds'] for rep in data['v155b']['reps'] if label in rep]
        if not v155a_times or not v20_times or not v155b_times:
            lines.append(f"| {label} | (missing) | (missing) | - | - | - |")
            ok = False
            continue
        med_155a = statistics.median(v155a_times)
        med_20 = statistics.median(v20_times)
        med_155b = statistics.median(v155b_times)
        delta = _pct_delta(med_20, med_155a)
        drift = _pct_delta(med_155b, med_155a)

        checksums_a = {rep[label]['checksum'] for rep in data['v155a']['reps'] if label in rep}
        checksums_20 = {rep[label]['checksum'] for rep in data['v20']['reps'] if label in rep}
        checksums_b = {rep[label]['checksum'] for rep in data['v155b']['reps'] if label in rep}
        checksum_match = 'yes' if (checksums_a | checksums_b) and (checksums_a == checksums_b) else 'FINDING: v1.5.5 drift'
        # cross-version checksum comparison is informational only (output types/formatting may
        # legitimately differ across engine versions); only same-version (a vs b) mismatch is a FINDING.
        if checksums_a != checksums_20:
            checksum_match += ' (v1.5.5 vs v2.0 checksum differs — see Findings)'

        lines.append(f"| {label} | {med_155a:.3f} | {med_20:.3f} | {delta:+.1f} | {drift:+.1f} | {checksum_match} |")

    return ok


def _compare_synth10k(lines: list) -> bool:
    ok = True
    lines.append("# 10k synthetic test comparison (timing only — random data)\n")
    lines.append("| label | wall s | summed statement s | delta % |")
    lines.append("|---|---|---|---|")

    synth_dir = RESULTS_ROOT / 'synth10k'
    base_file = synth_dir / 'v155.json'
    new_file = synth_dir / 'v20.json'
    if not base_file.exists() or not new_file.exists():
        lines.append("\nFINDING: missing synth10k result file(s)")
        return False

    base = json.loads(base_file.read_text())
    new = json.loads(new_file.read_text())
    lines.append(f"| v1.5.5 | {base['wall_seconds']:.2f} | {base['summed_statement_seconds']:.2f} | - |")
    delta_wall = _pct_delta(new['wall_seconds'], base['wall_seconds'])
    delta_sum = _pct_delta(new['summed_statement_seconds'], base['summed_statement_seconds'])
    lines.append(f"| v2.0 | {new['wall_seconds']:.2f} | {new['summed_statement_seconds']:.2f} | "
                 f"wall {delta_wall:+.1f} / stmts {delta_sum:+.1f} |")
    if base['statement_count'] != new['statement_count']:
        lines.append(f"\nFINDING: statement_count differs (v1.5.5={base['statement_count']}, "
                     f"v2.0={new['statement_count']})")
        ok = False
    return ok


def cmd_compare(args: argparse.Namespace) -> int:
    """Per plan: compare's exit code reflects ONLY whether all 19 expected M4 models are
    present for both labels. sqlbench/synth10k issues (including missing result files, which
    usually just means that step hasn't been run yet) are reported but do not fail this
    command — the report-writing step must still succeed with partial data."""
    EVID_DIR.mkdir(parents=True, exist_ok=True)

    m4_lines = []
    models_complete = _compare_m4(m4_lines)
    (EVID_DIR / 'm4-compare.md').write_text('\n'.join(m4_lines) + '\n')
    print(f"Wrote {EVID_DIR / 'm4-compare.md'} (models_complete={models_complete})")

    sqlbench_lines = []
    ok = _compare_sqlbench(sqlbench_lines)
    (EVID_DIR / 'sqlbench-compare.md').write_text('\n'.join(sqlbench_lines) + '\n')
    print(f"Wrote {EVID_DIR / 'sqlbench-compare.md'} (ok={ok})")

    synth_lines = []
    ok = _compare_synth10k(synth_lines)
    (EVID_DIR / 'synth10k-compare.md').write_text('\n'.join(synth_lines) + '\n')
    print(f"Wrote {EVID_DIR / 'synth10k-compare.md'} (ok={ok})")

    return 0 if models_complete else 1


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)

    p_tracer = sub.add_parser('tracer', help='Forecast-parity smoke test on a 20-series subset')
    p_tracer.add_argument('--base-ext', required=True, help='Path to the v1.5.5 extension')
    p_tracer.add_argument('--new-ext', required=True, help='Path to the v2.0 extension')
    p_tracer.set_defaults(func=cmd_tracer)

    p_m4 = sub.add_parser('m4', help='M4 Daily family benchmarks (baseline/ets/theta/arima/mfles/mstl)')
    p_m4.add_argument('--label', required=True, choices=['v155', 'v20'])
    p_m4.add_argument('--ext', required=True, help='Path to the extension to benchmark')
    p_m4.add_argument('--families', required=True,
                       help='Comma-separated family list, e.g. baseline,ets,theta,arima,mfles,mstl')
    p_m4.set_defaults(func=cmd_m4)

    p_sqlbench = sub.add_parser('sqlbench', help='SQL micro-benchmark (benchmark/sql/duckdb2_compare.sql)')
    p_sqlbench.add_argument('--label', required=True, help='Result label, e.g. v155a, v20, v155b')
    p_sqlbench.add_argument('--ext', required=True, help='Path to the extension to benchmark')
    p_sqlbench.add_argument('--reps', type=int, default=3)
    p_sqlbench.set_defaults(func=cmd_sqlbench)

    p_synth10k = sub.add_parser('synth10k', help='10k-series synthetic timing test')
    p_synth10k.add_argument('--label', required=True, choices=['v155', 'v20'])
    p_synth10k.add_argument('--root', required=True, help='Tree root whose build/release to measure')
    p_synth10k.set_defaults(func=cmd_synth10k)

    p_compare = sub.add_parser('compare', help='Write m4/sqlbench/synth10k comparison markdown to evidence/')
    p_compare.set_defaults(func=cmd_compare)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return args.func(args)


if __name__ == '__main__':
    sys.exit(main())
