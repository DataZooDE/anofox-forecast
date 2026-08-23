#!/usr/bin/env python3
"""statsmodels cross-check for ts_kpss / ts_stationarity (STAT-02, STAT-03).

Mirrors the design of run_anofox.py: because statsmodels and anofox differ in
bandwidth / lag selection, this validates BEHAVIORAL properties rather than exact
numeric parity. It drives the built DuckDB extension through the CLI (subprocess)
to avoid the Python duckdb package version mismatch.

Checks:
  KPSS
    1. classification direction: a random walk is judged non-stationary while a
       mean-reverting series is judged stationary (matches statsmodels kpss sign)
    2. statistic is non-negative and finite
  Combined verdict (ts_stationarity)
    3. verdict is one of the four labels
    4. a clear random walk classifies as 'difference_stationary' (both tests flag a unit root)

Usage:
    benchmark/.venv/bin/python benchmark/diagnostics/crosscheck_kpss.py
    # or: cd benchmark && uv run python diagnostics/crosscheck_kpss.py
"""

import json
import subprocess
import sys
import warnings

try:
    from statsmodels.tsa.stattools import kpss as sm_kpss
except ImportError as e:  # pragma: no cover
    print(
        f"ERROR: statsmodels is not available ({e}).\n"
        "Run inside the benchmark venv:\n"
        "  benchmark/.venv/bin/python benchmark/diagnostics/crosscheck_kpss.py",
        file=sys.stderr,
    )
    sys.exit(1)

DUCKDB = "./build/release/duckdb"
EXT = "./build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension"


def lcg(n, seed):
    """Deterministic pseudo-random uniform(-1, 1) sequence (no system RNG)."""
    x = seed
    out = []
    for _ in range(n):
        x = (x * 1664525 + 1013904223) & 0xFFFFFFFFFFFFFFFF
        out.append((x / 0xFFFFFFFFFFFFFFFF) * 2.0 - 1.0)
    return out


def random_walk(n, seed):
    steps = lcg(n, seed)
    s, acc = [], 0.0
    for v in steps:
        acc += v
        s.append(acc)
    return s


def ar1(n, seed, phi=0.3):
    noise = lcg(n, seed)
    s, prev = [], 0.0
    for v in noise:
        prev = phi * prev + 0.2 * v
        s.append(prev)
    return s


def run_sql(series, fn, fields):
    """Return selected STRUCT fields for fn(LIST(series)) from the built extension.

    `fields` is a list of STRUCT field names to project as top-level columns
    (avoids the json extension dependency of to_json).
    """
    lst = ", ".join(f"{v:.10f}" for v in series)
    proj = ", ".join(f"(s).{name} AS {name}" for name in fields)
    sql = (
        f"LOAD '{EXT}';\n"
        f"SELECT {proj} FROM (SELECT {fn}([{lst}]::DOUBLE[]) AS s);"
    )
    proc = subprocess.run(
        [DUCKDB, "-json", "-c", sql], capture_output=True, text=True
    )
    if proc.returncode != 0:
        raise RuntimeError(f"duckdb failed: {proc.stderr}")
    return json.loads(proc.stdout)[0]


def main():
    failures = []
    checks = 0

    rw = random_walk(160, 42)
    st = ar1(160, 42)

    # --- KPSS behavioral checks ---
    rw_k = run_sql(rw, "ts_kpss", ["statistic", "is_stationary"])
    st_k = run_sql(st, "ts_kpss", ["statistic", "is_stationary"])

    # statsmodels reference direction (suppress interpolation warnings)
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        rw_ref = sm_kpss(rw, regression="c", nlags="auto")[0]
        st_ref = sm_kpss(st, regression="c", nlags="auto")[0]

    checks += 1
    if not (rw_k["statistic"] > st_k["statistic"]):
        failures.append(
            f"KPSS statistic: random walk ({rw_k['statistic']:.4f}) should exceed "
            f"stationary ({st_k['statistic']:.4f})"
        )
    checks += 1
    if not (rw_ref > st_ref):
        failures.append("statsmodels KPSS reference direction unexpected")
    checks += 1
    if not (rw_k["is_stationary"] is False):
        failures.append("KPSS should judge the random walk non-stationary")
    checks += 1
    if not (st_k["is_stationary"] is True):
        failures.append("KPSS should judge the mean-reverting series stationary")
    checks += 1
    if not (rw_k["statistic"] >= 0.0 and st_k["statistic"] >= 0.0):
        failures.append("KPSS statistics must be non-negative")

    # --- Combined verdict checks ---
    labels = {"stationary", "trend_stationary", "difference_stationary", "non_stationary"}
    rw_v = run_sql(rw, "ts_stationarity", ["verdict", "adf_is_stationary", "kpss_is_stationary"])
    checks += 1
    if rw_v["verdict"] not in labels:
        failures.append(f"verdict {rw_v['verdict']!r} not one of {labels}")
    checks += 1
    if rw_v["verdict"] != "difference_stationary":
        failures.append(
            f"random walk verdict should be 'difference_stationary', got {rw_v['verdict']!r}"
        )

    print(f"KPSS random walk: statistic={rw_k['statistic']:.4f} is_stationary={rw_k['is_stationary']}")
    print(f"KPSS mean-revert: statistic={st_k['statistic']:.4f} is_stationary={st_k['is_stationary']}")
    print(f"Combined verdict (random walk): {rw_v['verdict']}")

    if failures:
        print(f"\n{len(failures)} FAILED / {checks} checks:", file=sys.stderr)
        for f in failures:
            print(f"  ✗ {f}", file=sys.stderr)
        sys.exit(1)
    print(f"\nAll {checks}/{checks} KPSS + stationarity cross-checks pass.")


if __name__ == "__main__":
    main()
