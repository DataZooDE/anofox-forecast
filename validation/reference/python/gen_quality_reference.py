"""Independent numpy/scipy reference implementation of `compute_data_quality`
(family `quality_scores`).

Writes `validation/reference/fixtures/quality_scores/cases.json`: for each case, the
input `values` (with `null`/`"NaN"` entries where relevant), optional `dates` +
`frequency` + `n_short`, and every documented score (`structural_score`,
`temporal_score`, `magnitude_score`, `behavioral_score`, `overall_score`, `n_gaps`,
`n_gap_runs`, `n_missing`, `is_constant`, `is_short`) computed by an INDEPENDENT
reimplementation of the documented formulas (not a call into the product) -- this is
the ground truth Phase 13 plan 13-03 task 2 (D-05/D-06, STAT-04) pins
`anofox_fcst_core::quality::compute_data_quality` against.

Run via: uv run --locked --project validation/reference/python python \
  validation/reference/python/gen_quality_reference.py
"""

from __future__ import annotations

import math
from typing import Optional

import numpy as np
import pandas as pd
from scipy import stats

from fixture_io import generator_path, tool_versions, write_fixture

TOLERANCE = {"score_abs": 1e-12}

MADN_CONST = 1.482602218505602  # 1 / scipy.stats.norm.ppf(0.75)


def is_missing(v: Optional[float]) -> bool:
    return v is None or (isinstance(v, float) and math.isnan(v))


def present_values(values: list[Optional[float]]) -> list[float]:
    return [v for v in values if not is_missing(v)]


def structural_score(n_present: int, n: int, n_short: int) -> float:
    if n == 0:
        return 0.0
    completeness = n_present / n
    length_factor = min(1.0, n_present / n_short)
    return float(np.clip(completeness * 0.7 + length_factor * 0.3, 0.0, 1.0))


def temporal_score_from_counts(n_missing_periods: int, expected_length: int) -> float:
    if expected_length == 0:
        return 1.0
    gap_ratio = n_missing_periods / expected_length
    return float(np.clip(1.0 - gap_ratio * 5.0, 0.0, 1.0))


def magnitude_score(present: list[float]) -> float:
    if not present:
        return 0.0
    x = np.array(present, dtype=float)
    n = x.shape[0]
    sorted_x = np.sort(x)
    q1 = sorted_x[int(n * 0.25)]
    q3 = sorted_x[int(n * 0.75)]
    iqr = q3 - q1
    lower = q1 - 1.5 * iqr
    upper = q3 + 1.5 * iqr
    outlier_ratio = float(np.sum((x < lower) | (x > upper))) / n

    mean = float(np.mean(x))
    std = float(np.std(x, ddof=0))
    extreme_ratio = float(np.sum(np.abs(x - mean) > 4.0 * std)) / n

    return float(np.clip(1.0 - outlier_ratio * 2.0 - extreme_ratio * 3.0, 0.0, 1.0))


def longest_zero_run(values: list[Optional[float]]) -> int:
    longest = 0
    current = 0
    for v in values:
        if is_missing(v):
            current = 0
        elif float(v) == 0.0:
            current += 1
            longest = max(longest, current)
        else:
            current = 0
    return longest


def behavioral_score(values: list[Optional[float]], present: list[float]) -> float:
    n_present = len(present)
    if n_present < 3:
        return 0.5

    x = np.array(present, dtype=float)
    variance = float(np.var(x, ddof=0))
    if abs(variance) < np.finfo(float).eps:
        return 0.0

    median = float(np.median(x))
    mad = float(stats.median_abs_deviation(x, scale=1.0))
    madn = MADN_CONST * mad
    if abs(madn) < np.finfo(float).eps:
        outlier_ratio = 0.0
    else:
        z = np.abs((x - median) / madn)
        outlier_ratio = float(np.sum(z > 3.5)) / n_present

    zero_run_ratio = longest_zero_run(values) / n_present

    return float(np.clip(1.0 - 2.0 * outlier_ratio - zero_run_ratio, 0.0, 1.0))


def gap_counts_fixed(full: pd.DatetimeIndex, kept_set: set) -> tuple[int, int, int]:
    """(expected_length, n_missing_periods, n_gap_runs) for a full regular grid vs
    the subset of timestamps actually kept -- mirrors gaps::gap_counts for the Fixed
    step case (mirrors gen_gap_counts.py's runs_of_missing)."""
    expected_length = len(full)
    n_missing_periods = expected_length - len(kept_set)
    runs = 0
    in_run = False
    for ts in full:
        missing = ts not in kept_set
        if missing and not in_run:
            runs += 1
            in_run = True
        elif not missing:
            in_run = False
    return expected_length, n_missing_periods, runs


def encode_values(values: list[Optional[float]]) -> list:
    out = []
    for v in values:
        if v is None:
            out.append(None)
        elif isinstance(v, float) and math.isnan(v):
            out.append("NaN")
        else:
            out.append(float(v))
    return out


def build_case(
    name: str,
    values: list[Optional[float]],
    n_short: int,
    *,
    dates: Optional[list[str]] = None,
    frequency: Optional[str] = None,
    expected_length: Optional[int] = None,
    n_missing_periods: Optional[int] = None,
    n_gap_runs: Optional[int] = None,
) -> dict:
    n = len(values)
    present = present_values(values)
    n_present = len(present)
    n_missing = n - n_present

    is_constant = n_present < 2 or all(
        abs(v - present[0]) < np.finfo(float).eps for v in present
    )

    if dates is not None:
        assert expected_length is not None
        assert n_missing_periods is not None
        assert n_gap_runs is not None
        temporal = temporal_score_from_counts(n_missing_periods, expected_length)
        n_gaps_out = n_missing_periods
        n_gap_runs_out = n_gap_runs
    else:
        temporal = 1.0
        n_gaps_out = 0
        n_gap_runs_out = 0

    structural = structural_score(n_present, n, n_short)
    magnitude = magnitude_score(present)
    behavioral = behavioral_score(values, present)
    overall = (structural + temporal + magnitude + behavioral) / 4.0

    case = {
        "name": name,
        "values": encode_values(values),
        "n_short": n_short,
        "frequency": frequency,
        "structural_score": structural,
        "temporal_score": temporal,
        "magnitude_score": magnitude,
        "behavioral_score": behavioral,
        "overall_score": overall,
        "n_gaps": n_gaps_out,
        "n_gap_runs": n_gap_runs_out,
        "n_missing": n_missing,
        "is_constant": is_constant,
        "is_short": n_present < n_short,
    }
    if dates is not None:
        case["dates"] = dates
    return case


def main() -> None:
    cases: dict[str, dict] = {}

    # --- smooth_sine100: slowly varying, |ACF1| > 0.95 -> behavioral 1.0 ---
    n = 100
    # Phase-shifted so no sample lands exactly on a zero crossing (a "run of exact
    # zeros" is a different, unrelated signal this case must not trip).
    smooth = [math.sin(2 * math.pi * i / 50.0 + 0.1) for i in range(n)]
    acf1 = float(np.corrcoef(smooth[:-1], smooth[1:])[0, 1])
    assert abs(acf1) > 0.95, f"smooth_sine100: expected |ACF1| > 0.95, got {acf1}"
    cases["smooth_sine100"] = build_case("smooth_sine100", smooth, n_short=3)
    assert abs(cases["smooth_sine100"]["behavioral_score"] - 1.0) < 1e-12

    # --- white_noise100: seeded rng -> behavioral 1.0 (no ACF1 penalty anymore) ---
    rng = np.random.default_rng(1301)
    noise = rng.normal(0.0, 1.0, 100).tolist()
    cases["white_noise100"] = build_case("white_noise100", noise, n_short=3)

    # --- constant10: constant series -> behavioral 0.0 ---
    cases["constant10"] = build_case("constant10", [5.0] * 10, n_short=3)
    assert cases["constant10"]["behavioral_score"] == 0.0

    # --- outliers100: 2 robust outliers in 100 values -> 1 - 2*0.02 = 0.96 ---
    base = [10.0 + 0.1 * ((i % 7) - 3) for i in range(100)]
    base[10] = 1000.0
    base[55] = -800.0
    cases["outliers100"] = build_case("outliers100", base, n_short=3)
    assert abs(cases["outliers100"]["behavioral_score"] - 0.96) < 1e-9, cases["outliers100"][
        "behavioral_score"
    ]

    # --- zero_run100: a 30-long zero run in 100 values -> 0.7 ---
    zero_run = [1.0] * 35 + [0.0] * 30 + [1.0] * 35
    cases["zero_run100"] = build_case("zero_run100", zero_run, n_short=3)
    assert abs(cases["zero_run100"]["behavioral_score"] - 0.7) < 1e-9, cases["zero_run100"][
        "behavioral_score"
    ]

    # --- nan_and_null: NaN and NULL entries both count as missing ---
    nan_and_null = [1.0, None, float("nan"), 4.0, 5.0, 6.0, 7.0, 8.0]
    cases["nan_and_null"] = build_case("nan_and_null", nan_and_null, n_short=3)
    assert cases["nan_and_null"]["n_missing"] == 2

    # --- daily_gap20: dates + '1d' frequency, 60 daily points, days 20..39 missing ---
    full = pd.date_range("2024-01-01", periods=60, freq="D")
    drop = set(range(20, 40))
    kept_positions = [i for i in range(60) if i not in drop]
    kept_set = {full[i] for i in kept_positions}
    expected_length, n_missing_periods, n_gap_runs = gap_counts_fixed(full, kept_set)
    gap_values = [float(i % 7 + 0.1 * i) for i in kept_positions]
    gap_dates = [full[i].isoformat() for i in kept_positions]
    cases["daily_gap20"] = build_case(
        "daily_gap20",
        gap_values,
        n_short=10,
        dates=gap_dates,
        frequency="1d",
        expected_length=expected_length,
        n_missing_periods=n_missing_periods,
        n_gap_runs=n_gap_runs,
    )
    assert cases["daily_gap20"]["n_gaps"] == 20
    assert cases["daily_gap20"]["n_gap_runs"] == 1
    assert cases["daily_gap20"]["temporal_score"] < 1.0

    # --- short_8: n_present=8, n_short=14 -> is_short True ---
    cases["short_8"] = build_case("short_8", [float(i) for i in range(8)], n_short=14)
    assert cases["short_8"]["is_short"] is True

    payload = {
        "tolerance": TOLERANCE,
        "cases": cases,
    }

    write_fixture(
        family="quality_scores",
        filename="cases.json",
        generator=generator_path(__file__),
        versions=tool_versions("numpy", "scipy", "pandas"),
        payload=payload,
    )


if __name__ == "__main__":
    main()
