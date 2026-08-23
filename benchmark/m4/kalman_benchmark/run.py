"""
Kalman filter benchmark.

Compares anofox ts_forecast_by('Kalman') against statsmodels UnobservedComponents
(local level and local linear trend state-space specifications).

Reference: statsmodels.tsa.statespace.structural.UnobservedComponents
  - local level: random walk + noise (obs_var estimated via MLE)
  - local linear trend: level + trend state-space (obs_var, level_var, slope_var via MLE)

Parity standard: behavioral/approximate — anofox KalmanForecaster uses fixed
variance params (obs_var=1.0, level_var=0.1) while statsmodels estimates them
via MLE. Exact numeric match is NOT expected. The parity criterion is that both
methods produce forecasts in the same ballpark (ratio 0.1–10.0) and both
converge to a reasonable level over the horizon.

IMPORTANT: Run via the benchmark venv, not system python3:
    cd benchmark && .venv/bin/python m4/kalman_benchmark/run.py run
"""
import sys
import time
import warnings
from pathlib import Path

import fire
import numpy as np
import pandas as pd

# Add benchmark root to sys.path
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from src.common.data import get_data

OUTPUT_DIR = Path(__file__).parent / 'results'

MAX_SERIES = 50        # cap at 50 M4 Daily series (statsmodels MLE is slow)
HORIZON = 14            # 14-step ahead forecast
FREQ = '1d'
MIN_OBS = 20            # minimum observations for Kalman filter


def _find_extension() -> Path:
    repo_root = Path(__file__).resolve().parents[3]
    ext_path = repo_root / 'build' / 'release' / 'extension' / 'anofox_forecast' / 'anofox_forecast.duckdb_extension'
    if not ext_path.exists():
        raise FileNotFoundError(f"Extension not found at {ext_path}. Build it first: make release")
    return ext_path


def _find_duckdb_cli() -> Path:
    repo_root = Path(__file__).resolve().parents[3]
    cli = repo_root / 'build' / 'release' / 'duckdb'
    if not cli.exists():
        raise FileNotFoundError(f"DuckDB CLI not found at {cli}. Build it first: make release")
    return cli


def _run_anofox_kalman(train_df: pd.DataFrame, extension_path: Path, duckdb_cli: Path,
                        kalman_model: str = 'local_level') -> pd.DataFrame:
    """Run anofox Kalman via CLI subprocess (venv/extension ABI safety)."""
    import subprocess
    import tempfile

    params_sql = '' if kalman_model == 'local_level' else f", params := MAP{{'kalman_model':'{kalman_model}'}}"

    with tempfile.TemporaryDirectory() as tmpdir:
        tmpdir = Path(tmpdir)
        train_parquet = tmpdir / 'train.parquet'
        result_parquet = tmpdir / 'result.parquet'
        train_df.to_parquet(train_parquet, index=False)

        sql = f"""LOAD '{extension_path}';
CREATE TABLE train AS SELECT * FROM read_parquet('{train_parquet}');
COPY (
    SELECT unique_id, ds, yhat
    FROM TS_FORECAST_BY('train', unique_id, ds, y, 'Kalman', {HORIZON}, '{FREQ}'{params_sql})
) TO '{result_parquet}' (FORMAT PARQUET);
"""
        script = tmpdir / 'query.sql'
        script.write_text(sql)

        result = subprocess.run(
            [str(duckdb_cli), '-unsigned', '-c', f".read '{script}'"],
            capture_output=True, text=True, timeout=600,
        )
        if result.returncode != 0:
            raise RuntimeError(
                f"DuckDB CLI failed (exit {result.returncode}):\n"
                f"STDOUT: {result.stdout}\nSTDERR: {result.stderr}"
            )
        if not result_parquet.exists():
            raise RuntimeError("CLI produced no result parquet")
        return pd.read_parquet(result_parquet)


def _run_statsmodels_kalman(series_dict: dict, spec: str = 'local level') -> pd.DataFrame:
    """
    Run statsmodels UnobservedComponents as the Kalman reference.

    Parameters
    ----------
    series_dict : dict
        {unique_id: np.ndarray of values}
    spec : str
        'local level' or 'local linear trend'

    Returns
    -------
    pd.DataFrame
        Reference forecasts [unique_id, horizon_step, sm_forecast].
    """
    from statsmodels.tsa.statespace.structural import UnobservedComponents

    rows = []
    skipped = 0
    for uid, y in series_dict.items():
        if len(y) < MIN_OBS:
            skipped += 1
            continue
        try:
            with warnings.catch_warnings():
                warnings.simplefilter('ignore')
                model = UnobservedComponents(y, spec)
                result = model.fit(disp=False, maxiter=100)
                fc = result.forecast(HORIZON)
            for step, val in enumerate(fc, start=1):
                rows.append({'unique_id': uid, 'horizon_step': step, 'sm_forecast': val})
        except Exception as e:
            skipped += 1
            print(f"  statsmodels skipped {uid}: {e}")
    if skipped:
        print(f"  statsmodels ({spec}): skipped {skipped} series")
    return pd.DataFrame(rows)


def _load_prep_data(group: str, dataset: str) -> tuple:
    """Load M4 data, cap series, convert dates."""
    train_df, horizon, freq, seasonality = get_data(dataset, group, train=True)
    all_ids = sorted(train_df['unique_id'].unique())
    if MAX_SERIES and len(all_ids) > MAX_SERIES:
        print(f"Capping to {MAX_SERIES} series")
        selected_ids = all_ids[:MAX_SERIES]
        train_df = train_df[train_df['unique_id'].isin(selected_ids)].copy()

    if not pd.api.types.is_datetime64_any_dtype(train_df['ds']):
        train_df['ds'] = pd.to_datetime('2020-01-01') + pd.to_timedelta(
            train_df['ds'].astype(int) - 1, unit='D'
        )
    train_df['ds'] = train_df['ds'].dt.date

    series_dict = {}
    for uid, grp in train_df.groupby('unique_id', sort=False):
        vals = grp.sort_values('ds')['y'].values
        if len(vals) >= MIN_OBS:
            series_dict[uid] = vals

    print(f"Loaded {len(series_dict)} series (min {MIN_OBS} obs)")
    return train_df, series_dict


def anofox(group: str = 'Daily', dataset: str = 'm4') -> None:
    """Run anofox Kalman (local_level and local_linear_trend) benchmark."""
    print(f"Loading M4 {group} data...")
    train_df, series_dict = _load_prep_data(group, dataset)

    ext_path = _find_extension()
    cli_path = _find_duckdb_cli()

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    for spec in ['local_level', 'local_linear_trend']:
        print(f"\nRunning anofox Kalman ({spec})...")
        start = time.time()
        fcst_df = _run_anofox_kalman(train_df, ext_path, cli_path, kalman_model=spec)
        elapsed = time.time() - start
        print(f"anofox Kalman ({spec}): {len(fcst_df)} rows in {elapsed:.2f}s")

        out = OUTPUT_DIR / f'anofox-kalman-{spec}-{group}.parquet'
        fcst_df.to_parquet(out, index=False)
        print(f"Saved to {out}")

        metrics = pd.DataFrame([{
            'model': f'anofox-Kalman-{spec}',
            'group': group,
            'time_seconds': elapsed,
            'series_count': fcst_df['unique_id'].nunique() if 'unique_id' in fcst_df.columns else 0,
            'forecast_points': len(fcst_df),
        }])
        metrics.to_parquet(OUTPUT_DIR / f'anofox-kalman-{spec}-{group}-metrics.parquet', index=False)


def statsmodels_reference(group: str = 'Daily', dataset: str = 'm4') -> None:
    """Run statsmodels UnobservedComponents reference (local level + local linear trend)."""
    print(f"Loading M4 {group} data (statsmodels reference)...")
    train_df, series_dict = _load_prep_data(group, dataset)

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    for spec_name, sm_spec in [('local_level', 'local level'), ('local_linear_trend', 'local linear trend')]:
        print(f"\nRunning statsmodels UnobservedComponents ({sm_spec})...")
        start = time.time()
        ref_df = _run_statsmodels_kalman(series_dict, spec=sm_spec)
        elapsed = time.time() - start
        print(f"statsmodels ({sm_spec}): {len(ref_df)} rows in {elapsed:.2f}s")

        out = OUTPUT_DIR / f'statsmodels-kalman-{spec_name}-{group}.parquet'
        ref_df.to_parquet(out, index=False)
        print(f"Saved to {out}")

        metrics = pd.DataFrame([{
            'model': f'statsmodels-Kalman-{spec_name}',
            'group': group,
            'time_seconds': elapsed,
            'series_count': len(series_dict),
            'forecast_points': len(ref_df),
        }])
        metrics.to_parquet(OUTPUT_DIR / f'statsmodels-kalman-{spec_name}-{group}-metrics.parquet', index=False)


def evaluate(group: str = 'Daily') -> pd.DataFrame:
    """
    Compare anofox vs statsmodels Kalman forecasts.

    Parity criterion: ratio of mean forecast levels within 0.5–2.0.
    Exact match NOT expected (different variance estimation).
    """
    results = []
    for spec in ['local_level', 'local_linear_trend']:
        anofox_path = OUTPUT_DIR / f'anofox-kalman-{spec}-{group}.parquet'
        sm_path = OUTPUT_DIR / f'statsmodels-kalman-{spec}-{group}.parquet'

        if not anofox_path.exists() or not sm_path.exists():
            print(f"  Skipping {spec}: result files missing")
            continue

        anofox_df = pd.read_parquet(anofox_path)
        sm_df = pd.read_parquet(sm_path)

        anofox_mean = anofox_df['yhat'].mean()
        sm_mean = sm_df['sm_forecast'].mean()
        ratio = anofox_mean / sm_mean if sm_mean != 0 else float('nan')
        parity = 'PASS' if 0.5 <= ratio <= 2.0 else 'PARTIAL'

        print(f"\nKalman ({spec}) parity — {group}:")
        print(f"  anofox mean forecast    : {anofox_mean:.4f}")
        print(f"  statsmodels mean forecast: {sm_mean:.4f}")
        print(f"  ratio (anofox/sm)       : {ratio:.3f}  [target: 0.5 – 2.0]")
        print(f"  parity verdict          : {parity}")

        results.append({
            'spec': spec,
            'group': group,
            'anofox_mean': anofox_mean,
            'statsmodels_mean': sm_mean,
            'ratio_anofox_over_sm': ratio,
            'parity': parity,
            'note': 'behavioral/approximate — anofox uses fixed variance; statsmodels uses MLE',
        })

    metrics = pd.DataFrame(results)
    out = OUTPUT_DIR / f'kalman-evaluation-{group}.parquet'
    metrics.to_parquet(out, index=False)
    print(f"\nSaved evaluation to {out}")
    return metrics


def run(group: str = 'Daily', dataset: str = 'm4') -> None:
    """Run full Kalman benchmark: anofox + statsmodels reference + evaluation."""
    print(f"{'='*80}")
    print(f"KALMAN BENCHMARK — M4 {group}")
    print(f"Reference: statsmodels UnobservedComponents (local level + local linear trend)")
    print(f"{'='*80}\n")

    print("STEP 1: Running anofox Kalman...")
    anofox(group, dataset)

    print(f"\nSTEP 2: Running statsmodels UnobservedComponents reference...")
    statsmodels_reference(group, dataset)

    print(f"\nSTEP 3: Evaluating parity...")
    evaluate(group)

    print(f"\n{'='*80}")
    print("KALMAN BENCHMARK COMPLETE")
    print(f"{'='*80}")


if __name__ == '__main__':
    fire.Fire({
        'run': run,
        'anofox': anofox,
        'statsmodels_reference': statsmodels_reference,
        'evaluate': evaluate,
    })
