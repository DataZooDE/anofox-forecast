"""Multi-scale OLS (DEP-01) reference generator (family `multiscale_ols`).

A faithful port of the audit's numpy-lstsq multi-scale OLS reproducer: eight
cases drawn from ONE shared `numpy.random.default_rng(1)` stream, in a fixed
order, so the final case (`exog_typical`) reproduces the audit-verified
coefficients exactly. Also writes a CSV companion (`exog_typical.csv`) for
the SQL-level `ts_forecast_exog_by` check in Phase 09-05.

Run via: uv run --locked --project validation/reference/python python \
  validation/reference/python/gen_multiscale_ols.py
"""

from __future__ import annotations

import datetime as dt

import numpy as np

from fixture_io import generator_path, tool_versions, write_csv, write_fixture

TOLERANCE = {
    "coef_rel": 1e-8,
    "coef_abs": 1e-9,
    "fitted_abs_per_sd": 1e-8,
    "sql_point_abs": 1.0,
    "notes": (
        "coef_rel/coef_abs/fitted_abs_per_sd are the Rust fit_ols_regression "
        "tolerance; sql_point_abs covers the residual-model component "
        "ts_forecast_exog_by adds on top of the regression effect, so the "
        "SQL-level point forecast is compared against expected_regression "
        "with a much looser bound."
    ),
}


def build_case(name: str, x: np.ndarray, beta: np.ndarray, rng: np.random.Generator, dep01_assert: bool, note: str) -> dict:
    """Draw noise LAST (`rng.normal(0, 0.5, x.shape[0])`), compute
    `y = x @ beta + 3.0 + noise`, fit `[1 | x] . coef = y` via numpy lstsq,
    and package the case for the fixture."""
    noise = rng.normal(0, 0.5, x.shape[0])
    y = x @ beta + 3.0 + noise
    n, p = x.shape
    a = np.column_stack([np.ones(n), x])
    coef, _residuals, rank, _sv = np.linalg.lstsq(a, y, rcond=None)
    fitted = a @ coef
    full_rank = bool(rank == p + 1)
    return {
        "n": n,
        "p": p,
        # Column-major: x_cols[j] is regressor j over all observations,
        # matching fit_ols_regression's `x: &[Vec<f64>]` layout.
        "X": [x[:, j] for j in range(p)],
        "y": y,
        "coef": coef,
        "fitted": fitted,
        "rank": int(rank),
        "full_rank": full_rank,
        "dep01_assert": dep01_assert,
        "note": note,
    }


def main() -> None:
    rng = np.random.default_rng(1)
    cases: dict[str, dict] = {}

    # 1. well: well-conditioned baseline.
    x = rng.normal(size=(100, 3))
    cases["well"] = build_case("well", x, np.array([1.0, -2.0, 0.5]), rng, True, "")

    # 2. multiscale: regressors spanning several orders of magnitude.
    x = np.column_stack(
        [
            rng.normal(1000, 300, 200),
            rng.normal(0, 1, 200),
            rng.normal(0, 0.01, 200),
            rng.integers(0, 2, 200).astype(float),
        ]
    )
    cases["multiscale"] = build_case("multiscale", x, np.array([0.01, 2.0, 50.0, -3.0]), rng, True, "")

    # 3. multiscale3: extreme scale spread (1e4 down to 1e-3).
    x = np.column_stack(
        [
            rng.normal(0, 1, 200) * 1e4,
            rng.normal(0, 1, 200),
            rng.normal(0, 1, 200) * 1e-3,
        ]
    )
    cases["multiscale3"] = build_case("multiscale3", x, np.array([1e-4, 1.0, 1e3]), rng, True, "")

    # 4. collinear: exact linear dependency (col3 = 2 * col1).
    x1 = rng.normal(size=200)
    col2 = rng.normal(size=200)
    x = np.column_stack([x1, col2, 2 * x1])
    cases["collinear"] = build_case(
        "collinear", x, np.array([1.0, 1.0, 0.0]), rng, True, "coefficients not unique, compare fitted values"
    )

    # 5. collinear_big: same dependency structure, large-scale regressor.
    x1 = rng.normal(1000, 100, 200)
    col2 = rng.normal(size=200)
    x = np.column_stack([x1, col2, 2 * x1])
    cases["collinear_big"] = build_case(
        "collinear_big", x, np.array([0.01, 1.0, 0.0]), rng, True, "coefficients not unique, compare fitted values"
    )

    # 6. constcol: a constant column collapses with the intercept.
    x = np.column_stack([rng.normal(size=200), np.full(200, 5.0)])
    cases["constcol"] = build_case(
        "constcol", x, np.array([1.0, 0.0]), rng, True, "coefficients not unique, compare fitted values"
    )

    # 7. n_lt_p: underdetermined (3 observations, 4 regressors).
    x = rng.normal(size=(3, 4))
    cases["n_lt_p"] = build_case(
        "n_lt_p",
        x,
        np.array([1.0, 1.0, 1.0, 1.0]),
        rng,
        False,
        "underdetermined, numpy returns the minimum-norm solution which no "
        "OLS-with-intercept library is expected to match; error behaviour is "
        "owned by Phase 10 FCST-05",
    )

    # 8. exog_typical: price/promo/temperature/holiday design, the audit's
    # ts_forecast_exog_by reproducer. Order matters: this is drawn from the
    # SAME rng stream after the seven cases above, so these exact coefficient
    # values depend on the full chain executing in this fixed order.
    x = np.column_stack(
        [
            rng.normal(10, 1, 365),
            rng.integers(0, 2, 365).astype(float),
            rng.normal(20, 8, 365),
            (rng.random(365) < 0.03).astype(float),
        ]
    )
    cases["exog_typical"] = build_case("exog_typical", x, np.array([-2.0, 5.0, 0.3, 10.0]), rng, True, "")

    payload = {
        "tolerance": TOLERANCE,
        "parameters": {
            "rng": "numpy.random.default_rng(1), one shared stream across all 8 cases in fixed order",
            "intercept_convention": "coef[0] is the intercept; coef[1:] are the betas in column order of X",
            "x_layout": "X is stored column-major: X[j] is regressor j over all n observations",
        },
        "cases": cases,
    }

    write_fixture(
        family="multiscale_ols",
        filename="cases.json",
        generator=generator_path(__file__),
        versions=tool_versions("numpy"),
        payload=payload,
    )

    # CSV companion: exog_typical history (365 rows) + future (5 rows), for
    # the SQL-level ts_forecast_exog_by check in 09-05. Column names x1..x4
    # are deliberate: ts_forecast_exog_by orders xreg columns by name, so
    # history and future stay aligned.
    exog = cases["exog_typical"]
    x_cols = exog["X"]  # [x1, x2, x3, x4], each length 365
    y = exog["y"]
    fitted = exog["fitted"]
    coef = exog["coef"]

    start = dt.date(2024, 1, 1)
    rows = []
    for i in range(365):
        d = start + dt.timedelta(days=i)
        rows.append(
            [
                "history",
                "s",
                d.isoformat(),
                float(x_cols[0][i]),
                float(x_cols[1][i]),
                float(x_cols[2][i]),
                float(x_cols[3][i]),
                float(y[i]),
                float(fitted[i]),
            ]
        )

    future_start = start + dt.timedelta(days=365)  # 2024-12-31
    for i in range(5):
        d = future_start + dt.timedelta(days=i)
        x1v, x2v, x3v, x4v = (float(x_cols[j][i]) for j in range(4))
        expected_regression = coef[0] + coef[1] * x1v + coef[2] * x2v + coef[3] * x3v + coef[4] * x4v
        rows.append(
            [
                "future",
                "s",
                d.isoformat(),
                x1v,
                x2v,
                x3v,
                x4v,
                None,
                float(expected_regression),
            ]
        )

    write_csv(
        family="multiscale_ols",
        filename="exog_typical.csv",
        header=["split", "sid", "date", "x1", "x2", "x3", "x4", "y", "expected_regression"],
        rows=rows,
    )


if __name__ == "__main__":
    main()
