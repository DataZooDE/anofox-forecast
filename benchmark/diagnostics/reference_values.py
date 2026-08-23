#!/usr/bin/env python3
"""Generate statsmodels reference values for the ADF stationarity cross-check.

This script produces a JSON file of reference ADF results using:
    statsmodels.tsa.stattools.adfuller(series, regression='c', autolag='AIC')

These are used by run_anofox.py to assert directional correctness of ts_adf.

IMPORTANT — Cross-check design notes
-------------------------------------
statsmodels and anofox use different automatic lag-selection criteria:
  - statsmodels AIC: searches lags in [0, maxlag], picks minimizer
  - anofox: max_lags = floor((n-1)^(1/3)), then AIC over [1, max_lags]

When they select different lag counts, the OLS regressions differ and
statistics cannot be compared numerically. The cross-check therefore
validates BEHAVIORAL properties only (not exact numerics):

  1. CLASSIFICATION: is_stationary matches expected direction
  2. STATISTIC SIGN: ADF statistic must be negative
  3. CRITICAL VALUES: cv_1pct, cv_5pct, cv_10pct match MacKinnon constants
     within rtol=0.10 (10% covers sample-size variation for n >= 30)
  4. NaN for short series: n < 4 → statistic = NaN, no error raised

Usage:
    benchmark/.venv/bin/python benchmark/diagnostics/reference_values.py
    # or from the benchmark directory:
    cd benchmark && uv run python diagnostics/reference_values.py

Requires:
    statsmodels >= 0.14
    scipy >= 1.10

Outputs:
    benchmark/diagnostics/reference_adf.json
"""

import json
import math
import os
import sys

# Check statsmodels availability before any heavy imports
try:
    import statsmodels.tsa.stattools as sm_stats
    import statsmodels
except ImportError as e:
    print(
        f"ERROR: statsmodels is not available ({e}).\n"
        "Install with: pip install statsmodels>=0.14\n"
        "Or, inside the benchmark venv:\n"
        "  benchmark/.venv/bin/python benchmark/diagnostics/reference_values.py",
        file=sys.stderr,
    )
    sys.exit(1)


# ---------------------------------------------------------------------------
# Deterministic fixture series using LCG pseudo-random number generator
# ---------------------------------------------------------------------------

def lcg_series(n, seed=42):
    """Linear congruential generator — deterministic, platform-independent."""
    a, c, m = 1664525, 1013904223, 2**32
    x = seed
    result = []
    for _ in range(n):
        x = (a * x + c) % m
        result.append((x / m - 0.5) * 2.0)  # scale to [-1, 1]
    return result


def make_series():
    """Return a dict of named deterministic fixture series.

    Series properties:
        white_noise     : iid uniform noise (clearly I(0), stationary)
        random_walk     : cumulative sum of LCG noise (clearly I(1), non-stationary)
        ar1_lcg         : AR(1) phi=0.5 driven by LCG noise (clearly stationary)
        short           : only 3 values — anofox returns NaN for n < 4

    All series are deterministic (no system random state). They were validated
    to produce sensible ADF statistics in both statsmodels and anofox with
    auto lag selection (AIC), avoiding ill-conditioning from overly regular patterns.
    """
    n = 80
    wn = lcg_series(n, seed=42)
    rw = [sum(wn[:i + 1]) for i in range(n)]

    noise = lcg_series(n, seed=99)
    ar1 = [0.0]
    for i in range(1, n):
        ar1.append(0.5 * ar1[-1] + noise[i])

    return {
        "white_noise":   (wn,  "stationary"),
        "random_walk":   (rw,  "nonstationary"),
        "ar1_lcg":       (ar1, "stationary"),
        "short":         ([1.0, 2.0, 3.0], "nan"),
    }


# MacKinnon (1994) asymptotic critical values for constant-only regression ('c')
# These are the n→∞ limits; actual values for n=80 are slightly less negative.
# run_anofox.py uses rtol=0.10 to handle the sample-size variation.
MACKINNON_ASYM = {
    "cv_1pct":  -3.43,
    "cv_5pct":  -2.86,
    "cv_10pct": -2.57,
}


def run_adf(series, max_lags=None):
    """Run statsmodels adfuller and return a flat result dict."""
    if len(series) < 4:
        return {
            "statistic": float("nan"),
            "p_value": float("nan"),
            "lags": 0,
            "error": "series too short (< 4 observations)",
        }
    try:
        result = sm_stats.adfuller(
            series,
            maxlag=max_lags,
            regression="c",
            autolag="AIC",
        )
        adf_stat, p_value, used_lag, nobs, crit, icbest = result
        return {
            "statistic": adf_stat,
            "p_value": p_value,
            "lags": used_lag,
            "nobs": nobs,
            "cv_1pct": crit["1%"],
            "cv_5pct": crit["5%"],
            "cv_10pct": crit["10%"],
            "error": None,
        }
    except Exception as exc:
        return {
            "statistic": float("nan"),
            "p_value": float("nan"),
            "lags": 0,
            "error": str(exc),
        }


def main():
    all_series = make_series()
    output = {
        "metadata": {
            "statsmodels_version": statsmodels.__version__,
            "regression": "c",
            "autolag": "AIC",
            "description": (
                "Reference ADF values from statsmodels.tsa.stattools.adfuller. "
                "Used by run_anofox.py for behavioral contract testing of ts_adf. "
                "Cross-check validates: (1) classification is_stationary, "
                "(2) negative statistic, (3) critical values near MacKinnon asymptotic. "
                "Exact numeric parity is NOT asserted (lag selection may differ)."
            ),
            "mackinnon_asymptotic": MACKINNON_ASYM,
            "cross_check_checks": [
                "is_stationary == expected_direction",
                "statistic < 0 (negative ADF t-stat)",
                "cv_1pct within 10% of -3.43",
                "cv_5pct within 10% of -2.86",
                "cv_10pct within 10% of -2.57",
                "statistic == NaN for short series (n < 4)",
            ],
        },
        "series": {},
    }

    for name, (data, expected_direction) in all_series.items():
        auto_result = run_adf(data)
        is_short = len(data) < 4

        output["series"][name] = {
            "data": data,
            "expected_direction": expected_direction,
            "sm_auto": auto_result,
        }

        if is_short:
            print(f"  {name}: short series → NaN expected")
        else:
            print(
                f"  {name}: stat={auto_result['statistic']:.4f} "
                f"p={auto_result['p_value']:.4f} lag={auto_result['lags']} "
                f"is_stat={auto_result['statistic'] < auto_result.get('cv_5pct', 0)} "
                f"→ {expected_direction}"
            )

    # Write output JSON
    out_dir = os.path.dirname(os.path.abspath(__file__))
    out_path = os.path.join(out_dir, "reference_adf.json")
    with open(out_path, "w") as f:
        json.dump(output, f, indent=2, allow_nan=True)

    print(f"\nWrote reference values to: {out_path}")
    print(f"statsmodels version: {statsmodels.__version__}")
    return 0


if __name__ == "__main__":
    print("Generating statsmodels ADF reference values...")
    sys.exit(main())
