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


def main() -> None:
    payload = {
        "two_group": build_two_group(),
        "interval_width": build_interval_width(),
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
