"""statsmodels ADF/KPSS reference generator (D-05/D-05a/D-04, Phase 17 plan 17-01).

Writes `validation/reference/fixtures/statsmodels_stationarity/adf_kpss_cases.json`:
for 8 series x regression in {"c", "ct"}, statsmodels `adfuller(autolag="AIC")`
and `kpss()` statistics / p-values / lags / critical values, plus the D-04
four-way stationarity verdict computed from the two tests' 5%-level rejection
flags. Five series (air, log_air, rw, diff_rw, ar1) are read BY EXPLICIT NAME
from the existing `r_diagnostics/stationarity_portmanteau.json` fixture
(Phase 9/11) -- later additions to that fixture must never change this
generator's series set. Three series are built here: `trend_stationary` (an
AR(1)-around-a-deterministic-trend series) and `diag_a` / `diag_b` (mirroring
`test/sql/ts_diagnostics.test`'s `diag_series` groups A / B).

`diag_a` and `diag_b` additionally carry ADF results at fixed `maxlag` 1 and 2
(to re-pin `ts_diagnostics.test`'s lag-override assertions). `diag_b` is
flagged `adf_statistic_comparable: false`: a sum of two sinusoids over only
40 points is fitted almost exactly by an AIC-selected 3-lag ADF regression,
making its statistic numerically ill-conditioned (not a meaningful ADF
value) -- `stationarity_reference.rs` skips the statistic/p-value assertion
for this series and asserts lags/nobs/critical-values only.

Cross-tool guard: for the five R-backed series, the statsmodels KPSS
statistic must equal R `tseries::kpss.test`'s `kpss_level` / `kpss_trend`
statistic (carried in the r_diagnostics fixture) within 1e-8, with equal
lags -- otherwise this script raises before writing anything.

Run via: uv run --locked --project validation/reference/python python \
  validation/reference/python/gen_stationarity_reference.py
"""

from __future__ import annotations

import json
import math

import numpy as np
from statsmodels.tsa.stattools import adfuller, kpss

from fixture_io import FIXTURES, generator_path, tool_versions, write_fixture

TOLERANCE = {
    "adf_statistic_abs": 1e-8,
    "adf_p_value_abs": 1e-8,
    "cv_abs": 1e-10,
    "kpss_statistic_abs": 1e-10,
    "kpss_p_value_abs": 1e-12,
}

R_SERIES_NAMES = ["air", "log_air", "rw", "diff_rw", "ar1"]
REGRESSIONS = ["c", "ct"]

#: D-04 four-way mapping, keyed on (adf_reject_5pct, kpss_reject_5pct):
#:   ADF rejects, KPSS does not reject  -> stationary
#:   ADF does not reject, KPSS rejects  -> non_stationary
#:   neither rejects                    -> trend_stationary
#:   both reject                        -> difference_stationary
VERDICTS = {
    (True, False): "stationary",
    (False, True): "non_stationary",
    (False, False): "trend_stationary",
    (True, True): "difference_stationary",
}


def load_r_diagnostics_series() -> tuple[dict[str, np.ndarray], dict]:
    path = FIXTURES / "r_diagnostics" / "stationarity_portmanteau.json"
    with open(path) as f:
        doc = json.load(f)
    series = {
        name: np.array(doc["series"][name]["values"], dtype=float)
        for name in R_SERIES_NAMES
    }
    return series, doc


def make_trend_stationary() -> np.ndarray:
    rng = np.random.default_rng(17002)
    e = rng.normal(0, 1, 150)
    u = np.zeros(150)
    for t in range(1, 150):
        u[t] = 0.7 * u[t - 1] + e[t]
    u = u[50:]
    t = np.arange(100, dtype=float)
    y = np.round(10 + 0.2 * t + u, 4) + 0.0
    return y


def make_diag_series() -> tuple[np.ndarray, np.ndarray]:
    i = np.arange(1, 41, dtype=float)
    incr = np.where((i.astype(int) % 7) < 4, 0.5, -0.3)
    diag_a = np.cumsum(incr)
    diag_b = 2.0 + 0.4 * np.sin(0.6 * i) + 0.1 * np.cos(1.2 * i)
    return diag_a, diag_b


def default_max_lag(n: int) -> int:
    """statsmodels/urca default when the caller does not fix one."""
    return int(math.floor((n - 1) ** (1.0 / 3.0)))


def default_kpss_lags(n: int) -> int:
    """The extension's default KPSS bandwidth: floor(4*(n/100)^0.25)."""
    return int(math.floor(4.0 * (n / 100.0) ** 0.25))


def adf_block(y: np.ndarray, reg: str, maxlag: int | None = None) -> dict:
    n = len(y)
    lag = default_max_lag(n) if maxlag is None else maxlag
    adfstat, pvalue, usedlag, nobs, critvalues, _icbest = adfuller(
        y, maxlag=lag, regression=reg, autolag="AIC", result_object=False
    )
    cv_5pct = float(critvalues["5%"])
    return {
        "statistic": float(adfstat),
        "p_value": float(pvalue),
        "usedlag": int(usedlag),
        "nobs": int(nobs),
        "cv_1pct": float(critvalues["1%"]),
        "cv_5pct": cv_5pct,
        "cv_10pct": float(critvalues["10%"]),
        "reject_5pct": bool(adfstat < cv_5pct),
    }


def kpss_block(y: np.ndarray, reg: str) -> dict:
    n = len(y)
    nlags = default_kpss_lags(n)
    stat, pvalue, lags, critvalues = kpss(
        y, regression=reg, nlags=nlags, result_object=False
    )
    cv_5pct = float(critvalues["5%"])
    return {
        "statistic": float(stat),
        "p_value": float(pvalue),
        "lags": int(lags),
        "cv_10pct": float(critvalues["10%"]),
        "cv_5pct": cv_5pct,
        "cv_2_5pct": float(critvalues["2.5%"]),
        "cv_1pct": float(critvalues["1%"]),
        "reject_5pct": bool(stat >= cv_5pct),
    }


def verdict_for(adf: dict, kpss_: dict) -> str:
    return VERDICTS[(adf["reject_5pct"], kpss_["reject_5pct"])]


def cross_tool_guard(name: str, y: np.ndarray, r_tests: dict) -> None:
    """For the five R-backed series: statsmodels KPSS must match R tseries
    (kpss_level for 'c', kpss_trend for 'ct') within 1e-8, equal lags."""
    r_block = {"c": r_tests["kpss_level"], "ct": r_tests["kpss_trend"]}
    for reg, r_entry in r_block.items():
        sm = kpss_block(y, reg)
        diff = abs(sm["statistic"] - r_entry["statistic"])
        if diff > 1e-8 or sm["lags"] != int(r_entry["lag"]):
            raise RuntimeError(
                f"cross_tool_guard failed for {name!r} regression={reg!r}: "
                f"statsmodels statistic={sm['statistic']!r} lags={sm['lags']!r} "
                f"vs R statistic={r_entry['statistic']!r} lag={r_entry['lag']!r} "
                f"(diff={diff!r})"
            )


def build_case(name: str, y: np.ndarray, *, comparable: bool = True) -> dict:
    n = int(len(y))
    regressions = {}
    for reg in REGRESSIONS:
        adf = adf_block(y, reg)
        kp = kpss_block(y, reg)
        regressions[reg] = {
            "adf": adf,
            "kpss": kp,
            "verdict": verdict_for(adf, kp),
        }
    case = {
        "name": name,
        "n": n,
        "values": y,
        "regressions": regressions,
    }
    if not comparable:
        case["adf_statistic_comparable"] = False
        case["adf_statistic_comparable_reason"] = (
            "sum of two sinusoids over 40 points is fitted almost exactly by "
            "an AIC-selected 3-lag ADF regression -- ill-conditioned, not a "
            "meaningful statistic"
        )
    return case


def main() -> None:
    r_series, r_doc = load_r_diagnostics_series()
    for name, y in r_series.items():
        cross_tool_guard(name, y, r_doc["series"][name]["tests"])

    trend_stationary = make_trend_stationary()
    diag_a, diag_b = make_diag_series()

    cases = []
    for name, y in r_series.items():
        cases.append(build_case(name, y))
    cases.append(build_case("trend_stationary", trend_stationary))
    cases.append(build_case("diag_a", diag_a))
    diag_b_case = build_case("diag_b", diag_b, comparable=False)
    # diag_b additionally carries fixed-maxlag ADF results (maxlag 1, 2) to
    # re-pin ts_diagnostics.test's lag-override assertions.
    diag_b_case["adf_fixed_maxlag"] = {
        "1": {reg: adf_block(diag_b, reg, maxlag=1) for reg in REGRESSIONS},
        "2": {reg: adf_block(diag_b, reg, maxlag=2) for reg in REGRESSIONS},
    }
    cases.append(diag_b_case)
    diag_a_case = next(c for c in cases if c["name"] == "diag_a")
    diag_a_case["adf_fixed_maxlag"] = {
        "1": {reg: adf_block(diag_a, reg, maxlag=1) for reg in REGRESSIONS},
        "2": {reg: adf_block(diag_a, reg, maxlag=2) for reg in REGRESSIONS},
    }

    write_fixture(
        family="statsmodels_stationarity",
        filename="adf_kpss_cases.json",
        generator=generator_path(__file__),
        versions=tool_versions("numpy", "statsmodels"),
        payload={
            "tolerance": TOLERANCE,
            "cases": cases,
        },
    )


if __name__ == "__main__":
    main()
