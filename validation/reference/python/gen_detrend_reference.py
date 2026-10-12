"""numpy polyfit reference generator (family `numpy_detrend`).

Writes `validation/reference/fixtures/numpy_detrend/polyfit_cases.json`: for
four seeded series, `numpy.polyfit` degree 1/2/3 fitted values, coefficients
(ascending powers of the raw 0-based time index `t = 0..n-1`), detrended
residuals, and RSS. Phase 15 plan 15-01 (DEP-03, WRAP-01, D-01) pins
`anofox_fcst_core::detrending::{detrend_linear,detrend_polynomial,detrend}`
against this fixture -- the fdars-core 0.47 bump makes `ts_detrend`/
`ts_detrend_by` non-trivial for the first time (B1 fix).

Run via: uv run --locked --project validation/reference/python python \
  validation/reference/python/gen_detrend_reference.py
"""

from __future__ import annotations

import numpy as np

from fixture_io import generator_path, tool_versions, write_fixture

TOLERANCE = {
    "trend_abs": 1e-8,
    "coef_rel": 1e-8,
    "coef_abs": 1e-10,
    "rss_rel": 1e-8,
}


def make_series() -> dict[str, np.ndarray]:
    t_cubic = np.arange(40, dtype=float)
    cubic40 = (
        0.002 * t_cubic**3 - 0.08 * t_cubic**2 + 0.5 * t_cubic + 3.0
        + np.random.default_rng(15001).normal(0, 0.3, 40)
    )
    cubic40 = np.round(cubic40, 4) + 0.0

    t_quad = np.arange(60, dtype=float)
    quadratic60 = (
        0.01 * t_quad**2 - 0.4 * t_quad + 5.0
        + np.random.default_rng(15002).normal(0, 0.5, 60)
    )
    quadratic60 = np.round(quadratic60, 4) + 0.0

    t_lin = np.arange(50, dtype=float)
    linear50 = (
        2.0 + 0.3 * t_lin
        + np.random.default_rng(15003).normal(0, 1, 50)
    )
    linear50 = np.round(linear50, 4) + 0.0

    const30 = np.full(30, 4.25, dtype=float)

    return {
        "cubic40": cubic40,
        "quadratic60": quadratic60,
        "linear50": linear50,
        "const30": const30,
    }


def fit_case(y: np.ndarray, degree: int) -> dict:
    n = y.shape[0]
    t = np.arange(n, dtype=float)
    c = np.polyfit(t, y, degree)  # descending powers
    fitted = np.polyval(c, t)
    coefficients = c[::-1]  # ascending powers: coefficients[k] is coef of t^k
    detrended = y - fitted
    rss = float(np.sum(detrended**2))
    return {
        "degree": degree,
        "fitted": fitted,
        "coefficients": coefficients,
        "detrended": detrended,
        "rss": rss,
    }


def main() -> None:
    series = make_series()
    cases = []
    for name, y in series.items():
        n = int(y.shape[0])
        degrees = [d for d in (1, 2, 3) if n >= d + 1]
        case = {
            "name": name,
            "n": n,
            "values": y,
            "degrees": {str(d): fit_case(y, d) for d in degrees},
        }
        cases.append(case)

    write_fixture(
        family="numpy_detrend",
        filename="polyfit_cases.json",
        generator=generator_path(__file__),
        versions=tool_versions("numpy"),
        payload={
            "tolerance": TOLERANCE,
            "cases": cases,
        },
    )


if __name__ == "__main__":
    main()
