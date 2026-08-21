"""
VAR multivariate benchmark — synthetic VAR(1) dataset.

Compares anofox ts_forecast_var_by against statsmodels.tsa.api.VAR on a synthetic
VAR(1) dataset with KNOWN coefficients. No M4 data needed (no multivariate M4 exists).

Synthetic data parameters (from RESEARCH Pattern 6):
  c = [0.5, 0.3]
  A = [[0.6, 0.1], [0.05, 0.7]]   # coefficient matrix
  N = 200 observations, seed = 42
  k = 2 variables (y1, y2)

Parity criterion:
  1. Coefficient recovery: fitted A matrix within 5% of ground truth.
  2. Forecast MAE: anofox MAE on held-out steps within 2x of statsmodels MAE.

IMPORTANT: Run via the benchmark venv, not system python3:
    cd benchmark && .venv/bin/python m4/var_benchmark/run.py run
"""
import sys
import subprocess
import tempfile
import time
from pathlib import Path

import fire
import numpy as np
import pandas as pd
import pyarrow as pa
import pyarrow.parquet as pq

# Add benchmark root to sys.path
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

OUTPUT_DIR = Path(__file__).parent / 'results'

# Synthetic VAR(1) parameters — known ground truth for coefficient recovery check
VAR1_C = [0.5, 0.3]
VAR1_A = [[0.6, 0.1], [0.05, 0.7]]
VAR1_N = 200
VAR1_SEED = 42
VAR1_K = 2                          # k=2 variables (y1, y2)

# Benchmark settings
HORIZON = 14                        # 14-step ahead forecast
FREQ = '1d'
START_DATE = '2020-01-01'
HELD_OUT = 14                       # hold out last HELD_OUT steps for MAE evaluation


def generate_var1_data(n: int = VAR1_N, c=None, a=None, seed: int = VAR1_SEED) -> np.ndarray:
    """
    Generate synthetic VAR(1) data with known coefficients.

    Model: y_t = c + A * y_{t-1} + epsilon_t
    where epsilon_t ~ Uniform(-0.01, 0.01) (small noise for near-deterministic ground truth).

    Parameters
    ----------
    n : int
        Number of observations.
    c : list
        Constant vector.
    a : list of list
        Coefficient matrix.
    seed : int
        Random seed.

    Returns
    -------
    np.ndarray
        Shape (n, k) — rows are time steps, cols are variables.
    """
    if c is None:
        c = VAR1_C
    if a is None:
        a = VAR1_A

    rng = np.random.default_rng(seed)
    k = len(c)
    y = np.zeros((n, k))
    y[0] = rng.uniform(-1, 1, k)
    a_mat = np.array(a)
    for t in range(1, n):
        noise = rng.uniform(-0.01, 0.01, k)
        y[t] = np.array(c) + a_mat @ y[t - 1] + noise
    return y


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


def _build_var_df(y: np.ndarray, start_date: str = START_DATE) -> pd.DataFrame:
    """Convert VAR data array to DataFrame with ds, y1, y2 columns."""
    n = y.shape[0]
    dates = pd.date_range(start=start_date, periods=n, freq='D').date
    return pd.DataFrame({
        'ds': dates,
        'y1': y[:, 0],
        'y2': y[:, 1],
    })


def _run_anofox_var(train_df: pd.DataFrame, extension_path: Path, duckdb_cli: Path,
                    p: int = 1) -> pd.DataFrame:
    """
    Run anofox ts_forecast_var_by on VAR data via CLI subprocess.

    CLI subprocess pattern avoids the venv/extension ABI version mismatch
    (same as Phase 2 panel benchmark and GARCH/Kalman above).
    """
    with tempfile.TemporaryDirectory() as tmpdir:
        tmpdir = Path(tmpdir)
        train_parquet = tmpdir / 'train.parquet'
        result_parquet = tmpdir / 'result.parquet'
        train_df.to_parquet(train_parquet, index=False)

        p_param = '' if p == 1 else f', p:={p}'

        sql = f"""LOAD '{extension_path}';
CREATE TABLE var_src AS SELECT * FROM read_parquet('{train_parquet}');
COPY (
    SELECT *
    FROM ts_forecast_var_by('var_src', 'ds', ['y1', 'y2'], {HORIZON}, '{FREQ}'{p_param})
) TO '{result_parquet}' (FORMAT PARQUET);
"""
        script = tmpdir / 'query.sql'
        script.write_text(sql)

        result = subprocess.run(
            [str(duckdb_cli), '-unsigned', '-c', f".read '{script}'"],
            capture_output=True, text=True, timeout=120,
        )
        if result.returncode != 0:
            raise RuntimeError(
                f"DuckDB CLI failed (exit {result.returncode}):\n"
                f"STDOUT: {result.stdout}\nSTDERR: {result.stderr}"
            )
        if not result_parquet.exists():
            raise RuntimeError("CLI produced no result parquet")
        return pd.read_parquet(result_parquet)


def _run_statsmodels_var(y_train: np.ndarray, p: int = 1) -> tuple:
    """
    Run statsmodels VAR(p) on the training data.

    Returns
    -------
    tuple
        (forecast array shape (HORIZON, k), fitted coefficients dict)
    """
    from statsmodels.tsa.api import VAR

    model = VAR(y_train)
    result = model.fit(maxlags=p, ic=None)
    forecast = result.forecast(y_train[-p:], steps=HORIZON)  # shape (HORIZON, k)

    # Extract coefficient matrix for ground-truth comparison
    # statsmodels stores coefs in result.params: shape (k*p + intercept, k)
    # For VAR(1): first k rows are the const, next k rows are lag-1 coefs
    coef_dict = {'fitted_A': result.coefs[0].tolist()}  # shape (k, k) for lag-1
    return forecast, coef_dict


def anofox_run(p: int = 1) -> None:
    """Run anofox ts_forecast_var_by on the synthetic VAR(1) dataset."""
    y = generate_var1_data()
    train_y = y[:-HELD_OUT]
    train_df = _build_var_df(train_y)
    print(f"Synthetic VAR(1) data: {len(train_df)} training obs, {HELD_OUT} held out")

    ext_path = _find_extension()
    cli_path = _find_duckdb_cli()

    print(f"Running anofox ts_forecast_var_by(p={p})...")
    start = time.time()
    fcst_df = _run_anofox_var(train_df, ext_path, cli_path, p=p)
    elapsed = time.time() - start
    print(f"anofox VAR({p}): {len(fcst_df)} long-format rows in {elapsed:.2f}s")
    print(f"  variables: {sorted(fcst_df['variable'].unique())}")

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    out = OUTPUT_DIR / f'anofox-var-p{p}.parquet'
    fcst_df.to_parquet(out, index=False)
    print(f"Saved anofox forecasts to {out}")

    metrics = pd.DataFrame([{
        'model': f'anofox-VAR({p})',
        'p': p,
        'time_seconds': elapsed,
        'forecast_rows': len(fcst_df),
        'variables': sorted(fcst_df['variable'].unique()),
    }])
    metrics.to_parquet(OUTPUT_DIR / f'anofox-var-p{p}-metrics.parquet', index=False)


def statsmodels_reference(p: int = 1) -> None:
    """Run statsmodels VAR(p) reference on the same synthetic VAR(1) dataset."""
    y = generate_var1_data()
    train_y = y[:-HELD_OUT]

    print(f"Running statsmodels VAR({p}) reference...")
    start = time.time()
    sm_forecast, coef_dict = _run_statsmodels_var(train_y, p=p)
    elapsed = time.time() - start

    # Convert to DataFrame matching anofox long format
    rows = []
    var_names = ['y1', 'y2']
    for vi, vname in enumerate(var_names):
        for step_idx in range(HORIZON):
            rows.append({
                'variable': vname,
                'horizon_step': step_idx + 1,
                'sm_forecast': sm_forecast[step_idx, vi],
            })
    ref_df = pd.DataFrame(rows)

    print(f"statsmodels VAR({p}): {len(ref_df)} rows in {elapsed:.2f}s")
    print(f"  Fitted A matrix: {coef_dict['fitted_A']}")
    print(f"  Ground truth A : {VAR1_A}")

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    out = OUTPUT_DIR / f'statsmodels-var-p{p}.parquet'
    ref_df.to_parquet(out, index=False)
    print(f"Saved statsmodels reference to {out}")

    # Save coefficient recovery metrics
    fitted_a = np.array(coef_dict['fitted_A'])
    true_a = np.array(VAR1_A)
    recovery_error = np.abs(fitted_a - true_a) / (np.abs(true_a) + 1e-8)
    coef_metrics = pd.DataFrame([{
        'model': f'statsmodels-VAR({p})',
        'p': p,
        'time_seconds': elapsed,
        'true_A': str(VAR1_A),
        'fitted_A': str(coef_dict['fitted_A']),
        'max_relative_error': float(recovery_error.max()),
        'mean_relative_error': float(recovery_error.mean()),
        'coefficient_recovery_within_5pct': bool(recovery_error.max() < 0.05),
    }])
    coef_metrics.to_parquet(OUTPUT_DIR / f'statsmodels-var-p{p}-coef-metrics.parquet', index=False)
    print(f"Coefficient recovery — max relative error: {recovery_error.max():.4f}")


def evaluate(p: int = 1) -> pd.DataFrame:
    """
    Compare anofox vs statsmodels VAR forecasts.

    Parity criteria:
    1. Forecast MAE on held-out steps: anofox MAE within 2x of statsmodels MAE.
    2. (Informational) statsmodels coefficient recovery within 5% of ground truth.
    """
    anofox_path = OUTPUT_DIR / f'anofox-var-p{p}.parquet'
    sm_path = OUTPUT_DIR / f'statsmodels-var-p{p}.parquet'

    if not anofox_path.exists():
        raise FileNotFoundError(f"Run 'anofox_run' step first: {anofox_path}")
    if not sm_path.exists():
        raise FileNotFoundError(f"Run 'statsmodels_reference' step first: {sm_path}")

    anofox_df = pd.read_parquet(anofox_path)
    sm_df = pd.read_parquet(sm_path)

    # Ground truth: held-out observations
    y = generate_var1_data()
    held_out = y[-HELD_OUT:]  # shape (HELD_OUT, 2)
    var_names = ['y1', 'y2']

    print(f"\n{'='*60}")
    print(f"VAR PARITY EVALUATION — Synthetic VAR(1)")
    print(f"{'='*60}")

    results = []
    for vi, vname in enumerate(var_names):
        true_vals = held_out[:, vi]

        # anofox forecasts for this variable
        af = anofox_df[anofox_df['variable'] == vname].sort_values('forecast_step')
        anofox_vals = af['forecast_value'].values[:HELD_OUT]

        # statsmodels forecasts for this variable
        sm = sm_df[sm_df['variable'] == vname].sort_values('horizon_step')
        sm_vals = sm['sm_forecast'].values[:HELD_OUT]

        n = min(len(true_vals), len(anofox_vals), len(sm_vals))
        anofox_mae = float(np.mean(np.abs(anofox_vals[:n] - true_vals[:n])))
        sm_mae = float(np.mean(np.abs(sm_vals[:n] - true_vals[:n])))
        ratio = anofox_mae / sm_mae if sm_mae > 0 else float('nan')

        parity = 'PASS' if ratio <= 2.0 else 'PARTIAL'

        print(f"\n  Variable {vname}:")
        print(f"    anofox MAE      : {anofox_mae:.6f}")
        print(f"    statsmodels MAE : {sm_mae:.6f}")
        print(f"    ratio (af/sm)   : {ratio:.3f}  [target: ≤ 2.0]")
        print(f"    parity verdict  : {parity}")

        results.append({
            'variable': vname,
            'p': p,
            'anofox_mae': anofox_mae,
            'statsmodels_mae': sm_mae,
            'ratio_anofox_over_sm': ratio,
            'parity': parity,
        })

    metrics = pd.DataFrame(results)
    out = OUTPUT_DIR / f'var-evaluation-p{p}.parquet'
    metrics.to_parquet(out, index=False)
    print(f"\nSaved evaluation to {out}")

    overall = 'PASS' if all(r['parity'] == 'PASS' for r in results) else 'PARTIAL'
    print(f"\nOverall parity: {overall}")
    print(f"{'='*60}")

    return metrics


def run(p: int = 1) -> None:
    """Run full VAR benchmark: generate data, anofox + statsmodels + evaluation."""
    print(f"{'='*80}")
    print(f"VAR BENCHMARK — Synthetic VAR(1) dataset")
    print(f"Ground truth: c={VAR1_C}, A={VAR1_A}, N={VAR1_N}, seed={VAR1_SEED}")
    print(f"Reference: statsmodels.tsa.api.VAR (OLS equation-by-equation)")
    print(f"{'='*80}\n")

    print("STEP 1: Running anofox ts_forecast_var_by...")
    anofox_run(p=p)

    print(f"\nSTEP 2: Running statsmodels VAR reference...")
    statsmodels_reference(p=p)

    print(f"\nSTEP 3: Evaluating parity...")
    evaluate(p=p)

    print(f"\n{'='*80}")
    print("VAR BENCHMARK COMPLETE")
    print(f"{'='*80}")


if __name__ == '__main__':
    fire.Fire({
        'run': run,
        'anofox_run': anofox_run,
        'statsmodels_reference': statsmodels_reference,
        'evaluate': evaluate,
    })
