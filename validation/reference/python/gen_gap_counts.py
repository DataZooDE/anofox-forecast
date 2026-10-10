"""pandas gap-count reference generator (family `gap_counts`).

Writes `validation/reference/fixtures/gap_counts/cases.json`: for each case, the full
expected `pandas.date_range` grid, the observed subset, and the resulting
`expected_length` / `n_missing_periods` / `n_gap_runs` ground truth. Also writes
`fill_cases`: observed + the full `expected_filled` grid, consumed by Phase 13 plan
13-03's month-end-aware gap fill.

Phase 13 (STAT-03, D-04) asserts `anofox_fcst_core::gaps::gap_counts` against this
fixture: `n_gaps` on `ts_stats` becomes `n_missing_periods` (missing periods), and the
new `n_gap_runs` column keeps the old meaning (count of gap intervals).

Run via: uv run --locked --project validation/reference/python python \
  validation/reference/python/gen_gap_counts.py
"""

from __future__ import annotations

import pandas as pd

from fixture_io import generator_path, tool_versions, write_fixture

ONE_DAY_MICROS = 86_400_000_000

TOLERANCE = {"count_abs": 0, "notes": "integer counts and exact timestamps"}


def to_iso(ts: pd.Timestamp) -> str:
    return ts.isoformat()


def runs_of_missing(full: pd.DatetimeIndex, kept: set[pd.Timestamp]) -> int:
    """Count contiguous runs of positions in `full` that are NOT in `kept`."""
    runs = 0
    in_run = False
    for ts in full:
        missing = ts not in kept
        if missing and not in_run:
            runs += 1
            in_run = True
        elif not missing:
            in_run = False
    return runs


def fixed_step_micros(full: pd.DatetimeIndex) -> int:
    assert len(full) >= 2, "fixed_step_micros: need at least 2 grid points"
    delta = full[1] - full[0]
    return int(delta.value // 1000)  # pandas Timedelta.value is nanoseconds


def build_case(
    name: str,
    sql_frequency: str,
    full: pd.DatetimeIndex,
    drop_positions: list[int],
    step: dict,
    *,
    duplicate_position: int | None = None,
) -> dict:
    kept_positions = [i for i in range(len(full)) if i not in drop_positions]
    kept_set = {full[i] for i in kept_positions}
    observed = [full[i] for i in kept_positions]
    if duplicate_position is not None:
        observed = observed + [full[duplicate_position]]

    expected_length = len(full)
    n_distinct_observed = len(kept_set)
    n_missing_periods = expected_length - n_distinct_observed
    n_gap_runs = runs_of_missing(full, kept_set)

    return {
        "name": name,
        "sql_frequency": sql_frequency,
        "step": step,
        "observed": [to_iso(ts) for ts in observed],
        "expected_length": expected_length,
        "n_missing_periods": n_missing_periods,
        "n_gap_runs": n_gap_runs,
    }


def build_fill_case(name: str, full: pd.DatetimeIndex, drop_positions: list[int]) -> dict:
    kept_positions = [i for i in range(len(full)) if i not in drop_positions]
    observed = [full[i] for i in kept_positions]
    return {
        "name": name,
        "observed": [to_iso(ts) for ts in observed],
        "expected_filled": [to_iso(ts) for ts in full],
    }


def main() -> None:
    cases: dict[str, dict] = {}
    fill_cases: dict[str, dict] = {}

    # --- daily_hole20: 60 daily points, days 20..39 missing (one 20-day run) ---
    full = pd.date_range("2024-01-01", periods=60, freq="D")
    micros = fixed_step_micros(full)
    cases["daily_hole20"] = build_case(
        "daily_hole20",
        "1d",
        full,
        list(range(20, 40)),
        {"kind": "fixed", "micros": micros},
    )

    # --- daily_no_gaps: 10 daily points, nothing missing ---
    full = pd.date_range("2024-01-01", periods=10, freq="D")
    micros = fixed_step_micros(full)
    cases["daily_no_gaps"] = build_case(
        "daily_no_gaps", "1d", full, [], {"kind": "fixed", "micros": micros}
    )

    # --- hourly_two_holes: 48 hourly points, two separate holes ---
    full = pd.date_range("2024-01-01", periods=48, freq="h")
    micros = fixed_step_micros(full)
    cases["hourly_two_holes"] = build_case(
        "hourly_two_holes",
        "1h",
        full,
        [10, 11, 12, 30, 31],
        {"kind": "fixed", "micros": micros},
    )

    # --- weekly: 12 weekly (W-MON) points, one missing week ---
    full = pd.date_range("2024-01-01", periods=12, freq="W-MON")
    micros = fixed_step_micros(full)
    cases["weekly"] = build_case(
        "weekly", "1w", full, [5], {"kind": "fixed", "micros": micros}
    )

    # --- month_start: 12 monthly (MS) points, two separate single-month holes ---
    full = pd.date_range("2024-01-01", periods=12, freq="MS")
    cases["month_start"] = build_case(
        "month_start", "1mo", full, [2, 7], {"kind": "months", "months": 1}
    )

    # --- month_end: explicit dates per plan (2024-01-31, 2024-04-30, 2024-05-31);
    # full ME grid Jan..May 2024 -> Feb29/Mar31 missing (1 run of 2) ---
    full = pd.date_range("2024-01-31", "2024-05-31", freq="ME")
    cases["month_end"] = build_case(
        "month_end", "1mo", full, [1, 2], {"kind": "months", "months": 1}
    )

    # --- quarter_end: 8 quarterly (QE-DEC) points, one missing quarter ---
    full = pd.date_range("2023-03-31", periods=8, freq="QE-DEC")
    cases["quarter_end"] = build_case(
        "quarter_end", "3mo", full, [3], {"kind": "months", "months": 3}
    )

    # --- year_start: 10 yearly (YS) points, 2 consecutive missing years ---
    full = pd.date_range("2015-01-01", periods=10, freq="YS")
    cases["year_start"] = build_case(
        "year_start", "1y", full, [4, 5], {"kind": "months", "months": 12}
    )

    # --- daily_duplicates: 10 daily points, no gaps, one duplicated timestamp ---
    full = pd.date_range("2024-01-01", periods=10, freq="D")
    micros = fixed_step_micros(full)
    cases["daily_duplicates"] = build_case(
        "daily_duplicates",
        "1d",
        full,
        [],
        {"kind": "fixed", "micros": micros},
        duplicate_position=3,
    )

    # --- fill_cases (consumed by plan 13-03) ---
    fill_cases["month_end"] = build_fill_case(
        "month_end", pd.date_range("2024-01-31", "2024-05-31", freq="ME"), [1, 2]
    )
    fill_cases["quarter_end"] = build_fill_case(
        "quarter_end", pd.date_range("2023-03-31", periods=8, freq="QE-DEC"), [3]
    )
    fill_cases["year_end"] = build_fill_case(
        "year_end", pd.date_range("2015-12-31", periods=10, freq="YE-DEC"), [4, 5]
    )
    fill_cases["month_start"] = build_fill_case(
        "month_start", pd.date_range("2024-01-01", periods=12, freq="MS"), [2, 7]
    )
    # mid_month_15: the 15th of each month; no clamping occurs (every month has a 15th).
    mid_month_full = pd.date_range(
        "2024-01-15", "2024-12-15", freq=pd.DateOffset(months=1)
    )
    fill_cases["mid_month_15"] = build_fill_case("mid_month_15", mid_month_full, [3, 4])

    payload = {
        "tolerance": TOLERANCE,
        "cases": cases,
        "fill_cases": fill_cases,
    }

    write_fixture(
        family="gap_counts",
        filename="cases.json",
        generator=generator_path(__file__),
        versions=tool_versions("pandas"),
        payload=payload,
    )


if __name__ == "__main__":
    main()
