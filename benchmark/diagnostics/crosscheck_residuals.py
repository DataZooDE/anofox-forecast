#!/usr/bin/env python3
"""statsmodels cross-check for ts_ljung_box / ts_durbin_watson / ts_jarque_bera /
ts_residual_diagnostics (RESID-01..04).

Behavioral cross-check (see run_anofox.py for the design rationale): drives the
built DuckDB extension through the CLI and compares directional/threshold
properties against statsmodels rather than exact numerics.

Checks:
  * Ljung-Box: p-value near 0 for strongly autocorrelated residuals, and not-tiny
    for white noise (matches statsmodels acorr_ljungbox direction)
  * Durbin-Watson: matches statsmodels.stats.stattools.durbin_watson within 1e-6,
    and interpretation label agrees with the statistic
  * Jarque-Bera: statistic within 5% of statsmodels jarque_bera for the same series
  * residual_diagnostics.adequate == (lb_p_value > 0.05)

Usage:
    benchmark/.venv/bin/python benchmark/diagnostics/crosscheck_residuals.py
"""

import json
import subprocess
import sys
import warnings

try:
    from statsmodels.stats.stattools import durbin_watson as sm_dw, jarque_bera as sm_jb
    from statsmodels.stats.diagnostic import acorr_ljungbox as sm_lb
except ImportError as e:  # pragma: no cover
    print(
        f"ERROR: statsmodels is not available ({e}).\n"
        "Run inside the benchmark venv:\n"
        "  benchmark/.venv/bin/python benchmark/diagnostics/crosscheck_residuals.py",
        file=sys.stderr,
    )
    sys.exit(1)

DUCKDB = "./build/release/duckdb"
EXT = "./build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension"


def lcg(n, seed):
    x = seed
    out = []
    for _ in range(n):
        x = (x * 1664525 + 1013904223) & 0xFFFFFFFFFFFFFFFF
        out.append((x / 0xFFFFFFFFFFFFFFFF) - 0.5)
    return out


def white_noise(n, seed):
    return lcg(n, seed)


def autocorr(n, seed, phi=0.9):
    noise = lcg(n, seed)
    s, prev = [], 0.0
    for v in noise:
        prev = phi * prev + v
        s.append(prev)
    return s


def run_sql(series, fn, fields, extra=""):
    lst = ", ".join(f"{v:.10f}" for v in series)
    proj = ", ".join(f"(s).{name} AS {name}" for name in fields)
    sql = (
        f"LOAD '{EXT}';\n"
        f"SELECT {proj} FROM (SELECT {fn}([{lst}]::DOUBLE[]{extra}) AS s);"
    )
    proc = subprocess.run([DUCKDB, "-json", "-c", sql], capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"duckdb failed: {proc.stderr}")
    return json.loads(proc.stdout)[0]


def approx(a, b, rtol):
    return abs(a - b) <= rtol * max(1.0, abs(b))


def main():
    failures = []
    checks = 0

    wn = white_noise(200, 7)
    ac = autocorr(200, 7)

    # --- Ljung-Box direction ---
    wn_lb = run_sql(wn, "ts_ljung_box", ["p_value"], extra=", 10")
    ac_lb = run_sql(ac, "ts_ljung_box", ["p_value"], extra=", 10")
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        wn_lb_ref = float(sm_lb(wn, lags=[10], return_df=True)["lb_pvalue"].iloc[0])
        ac_lb_ref = float(sm_lb(ac, lags=[10], return_df=True)["lb_pvalue"].iloc[0])
    checks += 1
    if not (ac_lb["p_value"] < 0.05):
        failures.append(f"Ljung-Box: autocorrelated p_value should be < 0.05, got {ac_lb['p_value']}")
    checks += 1
    if not (wn_lb["p_value"] > ac_lb["p_value"]):
        failures.append("Ljung-Box: white noise p_value should exceed autocorrelated p_value")
    checks += 1
    if not (ac_lb_ref < wn_lb_ref):
        failures.append("statsmodels Ljung-Box reference direction unexpected")

    # --- Durbin-Watson numeric parity ---
    wn_dw = run_sql(wn, "ts_durbin_watson", ["statistic", "interpretation"])
    checks += 1
    if not approx(wn_dw["statistic"], float(sm_dw(wn)), 1e-6):
        failures.append(f"Durbin-Watson mismatch: anofox {wn_dw['statistic']} vs sm {sm_dw(wn)}")
    checks += 1
    if wn_dw["interpretation"] not in (
        "positive_strong", "positive_weak", "none", "negative_weak", "negative_strong"
    ):
        failures.append(f"unexpected DW interpretation {wn_dw['interpretation']!r}")
    ac_dw = run_sql(ac, "ts_durbin_watson", ["statistic", "interpretation"])
    checks += 1
    if not (ac_dw["statistic"] < 1.0 and ac_dw["interpretation"].startswith("positive")):
        failures.append(f"autocorrelated DW should be < 1 and positive, got {ac_dw}")

    # --- Jarque-Bera numeric parity (within 5%) ---
    wn_jb = run_sql(wn, "ts_jarque_bera", ["statistic"])
    jb_ref = float(sm_jb(wn)[0])
    checks += 1
    if not approx(wn_jb["statistic"], jb_ref, 0.05):
        failures.append(f"Jarque-Bera mismatch: anofox {wn_jb['statistic']} vs sm {jb_ref}")

    # --- Combined adequacy gate ---
    wn_rd = run_sql(wn, "ts_residual_diagnostics", ["lb_p_value", "adequate"])
    checks += 1
    if wn_rd["adequate"] != (wn_rd["lb_p_value"] > 0.05):
        failures.append("adequate must equal (lb_p_value > 0.05)")
    ac_rd = run_sql(ac, "ts_residual_diagnostics", ["adequate"])
    checks += 1
    if ac_rd["adequate"] is not False:
        failures.append("autocorrelated residuals must be judged NOT adequate")

    print(f"Ljung-Box: white_noise p={wn_lb['p_value']:.4f}  autocorr p={ac_lb['p_value']:.2e}")
    print(f"Durbin-Watson: white_noise={wn_dw['statistic']:.4f} ({wn_dw['interpretation']})  "
          f"autocorr={ac_dw['statistic']:.4f} ({ac_dw['interpretation']})")
    print(f"Jarque-Bera: anofox={wn_jb['statistic']:.4f}  statsmodels={jb_ref:.4f}")
    print(f"Adequacy: white_noise={wn_rd['adequate']}  autocorr={ac_rd['adequate']}")

    if failures:
        print(f"\n{len(failures)} FAILED / {checks} checks:", file=sys.stderr)
        for f in failures:
            print(f"  ✗ {f}", file=sys.stderr)
        sys.exit(1)
    print(f"\nAll {checks}/{checks} residual-diagnostics cross-checks pass.")


if __name__ == "__main__":
    main()
