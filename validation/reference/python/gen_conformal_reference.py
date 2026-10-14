"""Independent numpy reference implementation of split conformal prediction
(family `conformal`, file `split_cases.json`).

Covers Phase 16 plan 16-01 (RUST-04): per-group AND pooled split-conformal
calibration (`conformity_score`, empirical `coverage`, `nominal_level`,
`n_calibration`) at two alpha levels, plus a pairwise-complete
`interval_width` case. Plans 16-02/16-03 extend this same generator/file with
asymmetric/adaptive and bootstrap cases -- do not rename the file or the
top-level `two_group`/`interval_width` keys.

Quantile convention (matches this codebase's Rust `conformal_quantile`,
unchanged by Phase 16 plan 16-01): for n calibration residuals and
miscoverage rate alpha, `k = ceil((n + 1) * (1 - alpha))`, and the
conformity score is the `k / n` quantile of `|residual|` using numpy's
default (`method="linear"`) interpolation. Empirical coverage is the
fraction of calibration residuals with `|residual| <= conformity_score`.

Run via: uv run --locked --project validation/reference/python python \
  validation/reference/python/gen_conformal_reference.py
"""

from __future__ import annotations

import math

import numpy as np

from fixture_io import generator_path, tool_versions, write_fixture

TOLERANCE = {"score_abs": 1e-9, "coverage_abs": 1e-12}

ALPHAS = [0.1, 0.2]


def date_str(year_month_day_start: tuple[int, int, int], offset_days: int) -> str:
    """Minimal date-string arithmetic (stdlib-free: avoids pulling in a date
    library just for +N days within a single month, matching the fixture's
    fixed Jan/Feb/Mar 2024 ranges)."""
    y, m, d = year_month_day_start
    d += offset_days
    # All ranges used in this fixture stay within one calendar month.
    return f"{y:04d}-{m:02d}-{d:02d}"


def split_calibration(residuals: list[float], alpha: float) -> dict:
    """Split-conformal calibration from a signed-residual pool."""
    abs_r = np.abs(np.asarray(residuals, dtype=np.float64))
    n = abs_r.shape[0]
    k = math.ceil((n + 1) * (1.0 - alpha))
    level = k / n
    conformity_score = float(np.quantile(abs_r, level, method="linear"))
    coverage = float(np.mean(abs_r <= conformity_score))
    return {
        "conformity_score": conformity_score,
        "coverage": coverage,
        "nominal_level": 1.0 - alpha,
        "n_calibration": n,
    }


def split_calibration_v2(residuals: list[float], alpha: float) -> dict:
    """Finite-sample split-conformal calibration (Phase 16 D-05/D-06 plan
    16-02): non-finite residuals (NaN/+-inf) are dropped BEFORE taking
    absolute values, and `k = ceil((n + 1) * (1 - alpha)) > n` (including
    `n == 0`) is reported as an explicit `calibration_insufficient` flag --
    never clamped to the max residual. `conformity_score`/`coverage` are
    `None` (-> JSON null -> Rust/SQL NaN/NULL) when insufficient."""
    arr = np.asarray(residuals, dtype=np.float64)
    finite = arr[np.isfinite(arr)]
    abs_r = np.abs(finite)
    n = int(abs_r.shape[0])
    insufficient = n == 0
    k = None
    if not insufficient:
        k = math.ceil((n + 1) * (1.0 - alpha))
        insufficient = k > n

    if insufficient:
        return {
            "conformity_score": None,
            "coverage": None,
            "nominal_level": 1.0 - alpha,
            "n_calibration": n,
            "calibration_insufficient": True,
        }

    level = k / n
    conformity_score = float(np.quantile(abs_r, level, method="linear"))
    coverage = float(np.mean(abs_r <= conformity_score))
    return {
        "conformity_score": conformity_score,
        "coverage": coverage,
        "nominal_level": 1.0 - alpha,
        "n_calibration": n,
        "calibration_insufficient": False,
    }


def build_two_group() -> dict:
    cal_dates = [date_str((2024, 1, 1), i) for i in range(20)]
    future_dates = [date_str((2024, 2, 1), i) for i in range(3)]

    scales = {"A": 1.0, "B": 10.0}
    future_points = {"A": [30.0, 10.0, 20.0], "B": [5.0, 50.0, 25.0]}

    groups: dict[str, dict] = {}
    all_residuals: list[float] = []

    for g, s in scales.items():
        actual = []
        forecast = []
        for i in range(20):
            sign = 1.0 if i % 2 == 0 else -1.0
            a = 100.0 + s * (i + 1) * 0.1 * sign
            f = 100.0
            actual.append(a)
            forecast.append(f)
        residuals = [a - f for a, f in zip(actual, forecast)]
        all_residuals.extend(residuals)

        alphas_out = {}
        for alpha in ALPHAS:
            cal = split_calibration(residuals, alpha)
            q = cal["conformity_score"]
            alphas_out[str(alpha)] = {
                **cal,
                "lower": [p - q for p in future_points[g]],
                "upper": [p + q for p in future_points[g]],
            }

        groups[g] = {
            "dates": cal_dates,
            "actual": actual,
            "forecast": forecast,
            "future_dates": future_dates,
            "future_point": future_points[g],
            "alphas": alphas_out,
        }

    pooled_alphas = {str(alpha): split_calibration(all_residuals, alpha) for alpha in ALPHAS}

    return {
        "groups": groups,
        "pooled": {"alphas": pooled_alphas},
    }


def build_interval_width() -> dict:
    rows = [
        {"date": "2024-03-01", "lower": 1.0, "upper": None},
        {"date": "2024-03-02", "lower": None, "upper": 5.0},
        {"date": "2024-03-03", "lower": 2.0, "upper": 4.0},
        {"date": "2024-03-04", "lower": 3.0, "upper": 9.0},
    ]

    def pairwise_complete(rs: list[dict]) -> dict:
        pairs = [(r["lower"], r["upper"]) for r in rs if r["lower"] is not None and r["upper"] is not None]
        widths = [u - l for (l, u) in pairs]
        return {
            "mean_width": float(np.mean(widths)) if widths else float("nan"),
            "n_intervals": len(widths),
        }

    return {
        "rows": rows,
        "full": pairwise_complete(rows),
        "first_three": pairwise_complete(rows[:3]),
    }


def build_insufficient_n5() -> dict:
    """5 calibration pairs, residuals 1..5: alpha 0.1 -> insufficient
    (ceil(6*0.9)=6 > 5); alpha 0.2 -> q 5.0, coverage 1.0 (ceil(6*0.8)=5 <= 5,
    level 1.0 -> max)."""
    actual = [101.0, 102.0, 103.0, 104.0, 105.0]
    forecast = [100.0] * 5
    residuals = [a - f for a, f in zip(actual, forecast)]
    alphas_out = {str(a): split_calibration_v2(residuals, a) for a in [0.1, 0.2]}
    return {
        "actual": actual,
        "forecast": forecast,
        "future_point": [10.0],
        "alphas": alphas_out,
    }


def build_n1() -> dict:
    """One calibration pair, residual 2.5: alpha 0.5 -> q 2.5 (ceil(2*0.5)=1
    <= 1); alpha 0.4 -> insufficient (ceil(2*0.6)=2 > 1)."""
    actual = [102.5]
    forecast = [100.0]
    residuals = [a - f for a, f in zip(actual, forecast)]
    alphas_out = {str(a): split_calibration_v2(residuals, a) for a in [0.4, 0.5]}
    return {"actual": actual, "forecast": forecast, "alphas": alphas_out}


def build_n0() -> dict:
    """Zero calibration pairs: always insufficient regardless of alpha."""
    alphas_out = {str(a): split_calibration_v2([], a) for a in [0.1, 0.5]}
    return {"alphas": alphas_out}


def build_all_nan() -> dict:
    """4 pairs whose actuals are all NaN -> n_calibration 0, insufficient."""
    actual = [float("nan")] * 4
    forecast = [100.0] * 4
    residuals = [a - f for a, f in zip(actual, forecast)]
    alphas_out = {"0.1": split_calibration_v2(residuals, 0.1)}
    return {"actual": actual, "forecast": forecast, "alphas": alphas_out}


def build_nan_filtered() -> dict:
    """two_group group A's construction (scale 1.0), with actual[3] = NaN
    (complete_cases drops this pair) and forecast[7] = inf (its residual is
    then -inf, dropped by the non-finite filter) -> n_calibration 18 out of
    20 original pairs."""
    actual = []
    forecast = []
    for i in range(20):
        sign = 1.0 if i % 2 == 0 else -1.0
        a = 100.0 + 1.0 * (i + 1) * 0.1 * sign
        f = 100.0
        actual.append(a)
        forecast.append(f)
    actual[3] = float("nan")
    forecast[7] = float("inf")
    residuals = [a - f for a, f in zip(actual, forecast)]
    alphas_out = {"0.1": split_calibration_v2(residuals, 0.1)}
    return {"actual": actual, "forecast": forecast, "alphas": alphas_out}


def build_invalid_alphas() -> list[float]:
    """alpha values outside (0, 1): D-06 rejects all of these naming the
    value, never silently defaulting or clamping."""
    return [0.0, 1.0, -0.1, 1.5]


def build_threshold_table() -> list[dict]:
    """For n = 0..60 and alpha in {0.01, 0.05, 0.1, 0.2, 0.25, 0.5, 0.9}: the
    insufficiency flag `ceil((n + 1) * (1 - alpha)) > n` (n = 0 always
    insufficient). Used to assert Rust's finite-sample level helper agrees
    with numpy for every (n, alpha) combination."""
    rows = []
    for n in range(0, 61):
        for alpha in [0.01, 0.05, 0.1, 0.2, 0.25, 0.5, 0.9]:
            if n == 0:
                insufficient = True
            else:
                k = math.ceil((n + 1) * (1.0 - alpha))
                insufficient = k > n
            rows.append({"n": n, "alpha": alpha, "insufficient": bool(insufficient)})
    return rows


def main() -> None:
    payload = {
        "two_group": build_two_group(),
        "interval_width": build_interval_width(),
        "insufficient_n5": build_insufficient_n5(),
        "n1": build_n1(),
        "n0": build_n0(),
        "all_nan": build_all_nan(),
        "nan_filtered": build_nan_filtered(),
        "invalid_alphas": build_invalid_alphas(),
        "threshold_table": build_threshold_table(),
        "tolerance": TOLERANCE,
    }
    out_path = write_fixture(
        family="conformal",
        filename="split_cases.json",
        generator=generator_path(__file__),
        versions=tool_versions("numpy"),
        payload=payload,
    )
    print(f"Wrote {out_path}")


if __name__ == "__main__":
    main()
