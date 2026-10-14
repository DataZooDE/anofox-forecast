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


def asymmetric_calibration(residuals: list[float], alpha: float) -> dict:
    """Phase 16 D-07 (plan 16-02): asymmetric margins from the FULL
    finite-residual count (not a positive/negative subset). `k = ceil((n +
    1) * (1 - alpha / 2))`; insufficient if `n == 0` or `k > n`. `upper =
    quantile(r, k/n)`; `lower = quantile(-r, k/n)`; `coverage = mean(-lower
    <= r <= upper)`; `conformity_score = (lower + upper) / 2`."""
    arr = np.asarray(residuals, dtype=np.float64)
    finite = arr[np.isfinite(arr)]
    n = int(finite.shape[0])
    half_alpha = alpha / 2.0
    insufficient = n == 0
    k = None
    if not insufficient:
        k = math.ceil((n + 1) * (1.0 - half_alpha))
        insufficient = k > n

    if insufficient:
        return {
            "lower_margin": None,
            "upper_margin": None,
            "coverage": None,
            "conformity_score": None,
            "nominal_level": 1.0 - alpha,
            "n_calibration": n,
            "calibration_insufficient": True,
        }

    level = k / n
    upper_margin = float(np.quantile(finite, level, method="linear"))
    lower_margin = float(np.quantile(-finite, level, method="linear"))
    coverage = float(np.mean((finite >= -lower_margin) & (finite <= upper_margin)))
    conformity_score = (lower_margin + upper_margin) / 2.0
    return {
        "lower_margin": lower_margin,
        "upper_margin": upper_margin,
        "coverage": coverage,
        "conformity_score": conformity_score,
        "nominal_level": 1.0 - alpha,
        "n_calibration": n,
        "calibration_insufficient": False,
    }


def build_asym_n20() -> dict:
    """Hand-checked (planner, 2026-10-12): lower margin 0.81, upper margin
    3.5125, coverage 0.9, conformity_score 2.16125 at alpha 0.2."""
    residuals = [-1.0, -0.8, -0.6, -0.4, -0.2] + [0.25 * k for k in range(1, 16)]
    return {
        "residuals": residuals,
        "future_point": [100.0],
        "alphas": {"0.2": asymmetric_calibration(residuals, 0.2)},
    }


def build_asym_n10() -> dict:
    """Same skewed residuals as the old positive/negative-subset test: at
    alpha 0.1, half_alpha=0.05, ceil(11*0.95)=11 > 10 -> insufficient."""
    residuals = [2.0, 3.0, 2.5, 3.5, 4.0, -0.5, -0.3, -0.2, -0.1, 2.8]
    return {"residuals": residuals, "alphas": {"0.1": asymmetric_calibration(residuals, 0.1)}}


def build_asym_all_positive() -> dict:
    """All-positive residuals 1..20 at alpha 0.2: the lower margin (quantile
    of the NEGATED residuals) is negative -- expected and documented, not a
    bug: a model that only ever under-predicts has no evidence its own point
    forecast is ever too high."""
    residuals = [float(i) for i in range(1, 21)]
    return {"residuals": residuals, "alphas": {"0.2": asymmetric_calibration(residuals, 0.2)}}


def build_asym_seeded_cases() -> dict:
    """3 seeded cases from a single `default_rng(16021)` stream (normal,
    centred log-normal, normal; n = 40, 99, 199): numpy margins at alpha 0.1
    for Rust to reproduce EXACTLY from the stored `residuals` array (the RNG
    only generates the fixture's fixed residual values here -- it is never
    re-run on the Rust side, unlike the independently-seeded Monte Carlo
    coverage simulation in monte_carlo.json)."""
    rng = np.random.default_rng(16021)
    sigma = 0.75
    cases = {}

    r40 = rng.standard_normal(40)
    cases["seeded_n40_normal"] = {
        "residuals": r40.tolist(),
        "alpha": 0.1,
        **asymmetric_calibration(r40.tolist(), 0.1),
    }

    r99_raw = rng.lognormal(mean=0.0, sigma=sigma, size=99)
    r99 = (r99_raw - math.exp(sigma**2 / 2.0)).tolist()
    cases["seeded_n99_lognormal"] = {
        "residuals": r99,
        "alpha": 0.1,
        **asymmetric_calibration(r99, 0.1),
    }

    r199 = rng.standard_normal(199).tolist()
    cases["seeded_n199_normal"] = {
        "residuals": r199,
        "alpha": 0.1,
        **asymmetric_calibration(r199, 0.1),
    }

    return cases


# ============================================================================
# Monte Carlo coverage simulation (Phase 16 D-07, CONF-03): independent numpy
# simulation of the SAME symmetric/asymmetric constructions Rust implements,
# compared via a coverage/width TOLERANCE band (not RNG-exact parity --
# Rust's own Monte Carlo test uses an independently-seeded inline SplitMix64
# generator, a different algorithm from numpy's PCG64 default_rng).
# ============================================================================

MC_TRIALS = 2000
MC_N_CAL = 199
MC_M_TEST = 25
MC_ALPHAS = [0.1, 0.2]
# Seeds for this file's OWN numpy simulation (distinct from the seeds
# 16101/16102/16103 Rust's inline generator uses for its independent run).
MC_SEEDS = {
    "symmetric_normal": 16041,
    "symmetric_lognormal": 16042,
    "asymmetric_normal": 16043,
    "asymmetric_lognormal": 16044,
    # Matches the seed Rust's inline SplitMix64 generator uses for its own
    # independent heteroscedastic simulation (plan 16-02 interfaces block).
    "adaptive_heteroscedastic": 16103,
}


def _mc_draw(rng: np.random.Generator, dist: str, n: int) -> np.ndarray:
    if dist == "normal":
        return rng.standard_normal(n)
    if dist == "lognormal":
        sigma = 0.75
        return rng.lognormal(mean=0.0, sigma=sigma, size=n) - math.exp(sigma**2 / 2.0)
    raise ValueError(f"unknown distribution: {dist}")


def _mc_old_asymmetric_margins(cal: np.ndarray, half_alpha: float) -> tuple[float, float]:
    """The OLD (pre-16-02) positive/negative-subset asymmetric construction,
    replicated here only to document the coverage/width regression it had
    (documentation for the 16-03 CHANGELOG), never used by Rust."""
    positive = cal[cal > 0]
    negative = np.abs(cal[cal < 0])
    if positive.size == 0:
        upper_margin = 0.0
    else:
        n = positive.size
        k = math.ceil((n + 1) * (1.0 - half_alpha))
        level = min(k / n, 1.0)
        upper_margin = float(np.quantile(positive, level, method="linear"))
    if negative.size == 0:
        lower_margin = 0.0
    else:
        n = negative.size
        k = math.ceil((n + 1) * (1.0 - half_alpha))
        level = min(k / n, 1.0)
        lower_margin = float(np.quantile(negative, level, method="linear"))
    return lower_margin, upper_margin


def monte_carlo_run(method: str, dist: str, seed: int) -> dict:
    """Runs MC_TRIALS trials ONCE per (method, dist), evaluating every alpha
    in MC_ALPHAS from the SAME draws (standard Monte Carlo practice -- not a
    fresh simulation per alpha)."""
    rng = np.random.default_rng(seed)
    acc = {a: {"covered": 0, "total": 0, "widths": []} for a in MC_ALPHAS}
    old_acc = (
        {a: {"covered": 0, "total": 0, "widths": []} for a in MC_ALPHAS}
        if method == "asymmetric"
        else None
    )

    for _ in range(MC_TRIALS):
        cal = _mc_draw(rng, dist, MC_N_CAL)
        test = _mc_draw(rng, dist, MC_M_TEST)
        for alpha in MC_ALPHAS:
            if method == "symmetric":
                abs_cal = np.abs(cal)
                k = math.ceil((MC_N_CAL + 1) * (1.0 - alpha))
                level = k / MC_N_CAL
                q = float(np.quantile(abs_cal, level, method="linear"))
                lower_m, upper_m = q, q
            else:
                half_alpha = alpha / 2.0
                k = math.ceil((MC_N_CAL + 1) * (1.0 - half_alpha))
                level = k / MC_N_CAL
                upper_m = float(np.quantile(cal, level, method="linear"))
                lower_m = float(np.quantile(-cal, level, method="linear"))

            acc[alpha]["covered"] += int(np.sum((test >= -lower_m) & (test <= upper_m)))
            acc[alpha]["total"] += MC_M_TEST
            acc[alpha]["widths"].append(lower_m + upper_m)

            if method == "asymmetric":
                old_lower, old_upper = _mc_old_asymmetric_margins(cal, alpha / 2.0)
                old_acc[alpha]["covered"] += int(
                    np.sum((test >= -old_lower) & (test <= old_upper))
                )
                old_acc[alpha]["total"] += MC_M_TEST
                old_acc[alpha]["widths"].append(old_lower + old_upper)

    out = {}
    for alpha in MC_ALPHAS:
        mean_width = float(np.mean(acc[alpha]["widths"]))
        entry = {
            "coverage": acc[alpha]["covered"] / acc[alpha]["total"],
            "mean_width": mean_width,
        }
        if method == "asymmetric":
            old_width = float(np.mean(old_acc[alpha]["widths"]))
            entry["old_construction"] = {
                "coverage": old_acc[alpha]["covered"] / old_acc[alpha]["total"],
                "mean_width": old_width,
            }
            entry["old_over_new_width_ratio"] = old_width / mean_width
        out[str(alpha)] = entry
    return out


def build_monte_carlo() -> dict:
    results: dict[str, dict] = {}
    for method in ["symmetric", "asymmetric"]:
        results[method] = {}
        for dist in ["normal", "lognormal"]:
            seed = MC_SEEDS[f"{method}_{dist}"]
            results[method][dist] = monte_carlo_run(method, dist, seed)
    results["adaptive"] = {
        "heteroscedastic": monte_carlo_run_adaptive(MC_SEEDS["adaptive_heteroscedastic"])
    }
    return {
        "params": {
            "trials": MC_TRIALS,
            "n_cal": MC_N_CAL,
            "m_test": MC_M_TEST,
            "alphas": MC_ALPHAS,
        },
        "seeds": MC_SEEDS,
        "results": results,
    }


def adaptive_calibration(residuals: list[float], difficulty: list[float], alpha: float) -> dict:
    """Phase 16 D-08 (plan 16-02): adaptive scores `s_i = |r_i| / d_i` over
    complete (finite residual, finite difficulty) pairs; `q` is their
    finite-sample quantile (same insufficiency rule as every other
    method). No batch-mean normalisation at prediction time -- callers
    multiply `q` by the RAW prediction difficulty."""
    r = np.asarray(residuals, dtype=np.float64)
    d = np.asarray(difficulty, dtype=np.float64)
    finite_mask = np.isfinite(r) & np.isfinite(d)
    scores = np.abs(r[finite_mask]) / d[finite_mask]
    n = int(scores.shape[0])
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
    q = float(np.quantile(scores, level, method="linear"))
    coverage = float(np.mean(scores <= q))
    return {
        "conformity_score": q,
        "coverage": coverage,
        "nominal_level": 1.0 - alpha,
        "n_calibration": n,
        "calibration_insufficient": False,
    }


def build_adaptive_n20() -> dict:
    """d_i = 1 + (i % 4); residual_i = (i + 1) * 0.1 * (+1 if i even else
    -1) * d_i; stored as actual = 100 + residual, forecast = 100. alpha 0.1
    -> q 1.905, coverage 0.95. Prediction point [10, 10, 10] with d [1, 2,
    0.5] -> half-widths [1.905, 3.81, 0.9525] (hand-checked by the
    planner)."""
    actual = []
    forecast = []
    difficulty = []
    for i in range(20):
        d = 1 + (i % 4)
        sign = 1.0 if i % 2 == 0 else -1.0
        r = (i + 1) * 0.1 * sign * d
        actual.append(100.0 + r)
        forecast.append(100.0)
        difficulty.append(float(d))
    residuals = [a - f for a, f in zip(actual, forecast)]
    return {
        "actual": actual,
        "forecast": forecast,
        "difficulty": difficulty,
        "future_point": [10.0, 10.0, 10.0],
        "future_difficulty": [1.0, 2.0, 0.5],
        "alphas": {"0.1": adaptive_calibration(residuals, difficulty, 0.1)},
    }


def build_adaptive_seeded_cases() -> dict:
    """2 seeded cases from `default_rng(16031)`: d ~ U(0.5, 3), r = d *
    N(0, 1), n = 99/199, alpha 0.1."""
    rng = np.random.default_rng(16031)
    cases = {}
    for n in (99, 199):
        d = rng.uniform(0.5, 3.0, size=n)
        z = rng.standard_normal(n)
        r = d * z
        cases[f"seeded_n{n}"] = {
            "residuals": r.tolist(),
            "difficulty": d.tolist(),
            "alpha": 0.1,
            **adaptive_calibration(r.tolist(), d.tolist(), 0.1),
        }
    return cases


def monte_carlo_run_adaptive(seed: int) -> dict:
    """Heteroscedastic Monte Carlo (adaptive only): `r = d * N(0, 1)` with
    `d ~ U(0.5, 3)` drawn per row at BOTH calibration and test."""
    rng = np.random.default_rng(seed)
    acc = {a: {"covered": 0, "total": 0, "widths": []} for a in MC_ALPHAS}

    for _ in range(MC_TRIALS):
        d_cal = rng.uniform(0.5, 3.0, size=MC_N_CAL)
        z_cal = rng.standard_normal(MC_N_CAL)
        r_cal = d_cal * z_cal
        d_test = rng.uniform(0.5, 3.0, size=MC_M_TEST)
        z_test = rng.standard_normal(MC_M_TEST)
        r_test = d_test * z_test

        scores = np.abs(r_cal) / d_cal
        for alpha in MC_ALPHAS:
            k = math.ceil((MC_N_CAL + 1) * (1.0 - alpha))
            level = k / MC_N_CAL
            q = float(np.quantile(scores, level, method="linear"))
            margin = q * d_test
            covered = int(np.sum(np.abs(r_test) <= margin))
            acc[alpha]["covered"] += covered
            acc[alpha]["total"] += MC_M_TEST
            acc[alpha]["widths"].append(float(np.mean(2.0 * margin)))

    return {
        str(alpha): {
            "coverage": acc[alpha]["covered"] / acc[alpha]["total"],
            "mean_width": float(np.mean(acc[alpha]["widths"])),
        }
        for alpha in MC_ALPHAS
    }


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


ASYMMETRIC_TOLERANCE = {"score_abs": 1e-9, "coverage_abs": 1e-12}
MONTE_CARLO_TOLERANCE = {"coverage_abs": 0.02, "mean_width_rel": 0.05}


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

    asymmetric_payload = {
        "asym_n20": build_asym_n20(),
        "asym_n10": build_asym_n10(),
        "asym_all_positive": build_asym_all_positive(),
        "seeded": build_asym_seeded_cases(),
        "tolerance": ASYMMETRIC_TOLERANCE,
    }
    out_path = write_fixture(
        family="conformal",
        filename="asymmetric_cases.json",
        generator=generator_path(__file__),
        versions=tool_versions("numpy"),
        payload=asymmetric_payload,
    )
    print(f"Wrote {out_path}")

    adaptive_payload = {
        "adaptive_n20": build_adaptive_n20(),
        "seeded": build_adaptive_seeded_cases(),
        "tolerance": ASYMMETRIC_TOLERANCE,
    }
    out_path = write_fixture(
        family="conformal",
        filename="adaptive_cases.json",
        generator=generator_path(__file__),
        versions=tool_versions("numpy"),
        payload=adaptive_payload,
    )
    print(f"Wrote {out_path}")

    monte_carlo_payload = {
        **build_monte_carlo(),
        "tolerance": MONTE_CARLO_TOLERANCE,
    }
    out_path = write_fixture(
        family="conformal",
        filename="monte_carlo.json",
        generator=generator_path(__file__),
        versions=tool_versions("numpy"),
        payload=monte_carlo_payload,
    )
    print(f"Wrote {out_path}")


if __name__ == "__main__":
    main()
