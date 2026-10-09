"""Panel NaN/Inf guard outcome reference generator (family `panel_nan_guard`,
DEP-02 reference).

Encodes four panel cases (interior gaps, an all-missing series, an infinite
value, and an intermittent-demand panel with interior gaps) with a
per-method EXPECTED OUTCOME CLASS (finite | error) — this is an
outcome-class fixture, not a point-value fixture: 0.15.10's panel guards
must never return `Ok` with a silent NaN/zero, and must never return `Ok`
with a non-finite forecast.

Run via: uv run --locked --project validation/reference/python python \
  validation/reference/python/gen_panel_nan_guard.py
"""

from __future__ import annotations

import numpy as np

from fixture_io import generator_path, tool_versions, write_fixture

TOLERANCE = {
    "abs": 0.0,
    "notes": (
        "Outcome-class fixture: 'finite' means the panel call returns Ok "
        "with every forecast finite; 'error' means the panel call returns "
        "Err (never Ok with NaN or silent zeros)."
    ),
}

LEVELS = (10.0, 20.0, 30.0, 40.0)
N_SERIES = 4
SERIES_LEN = 24


def make_trended_panel(rng: np.random.Generator) -> np.ndarray:
    """level[s] + 0.5*t + rng.normal(0, 0.3), one fresh draw per call."""
    t = np.arange(SERIES_LEN)
    vals = np.empty((N_SERIES, SERIES_LEN))
    for s in range(N_SERIES):
        vals[s] = LEVELS[s] + 0.5 * t + rng.normal(0, 0.3, SERIES_LEN)
    return vals


def main() -> None:
    rng = np.random.default_rng(20261009)
    cases: dict[str, dict] = {}

    # 1. interior_gaps: interior NaN in three of the four series.
    vals = make_trended_panel(rng)
    vals[0, 5] = np.nan
    vals[1, 11] = np.nan
    vals[2, 17] = np.nan
    cases["interior_gaps"] = {
        "n_series": N_SERIES,
        "series_len": SERIES_LEN,
        "values": vals,
        "expect": {"GlobalETS": "finite", "GlobalTheta": "finite"},
    }

    # 2. all_missing_series: a fresh trended draw with series 1 entirely NaN.
    vals = make_trended_panel(rng)
    vals[1, :] = np.nan
    cases["all_missing_series"] = {
        "n_series": N_SERIES,
        "series_len": SERIES_LEN,
        "values": vals,
        "expect": {"GlobalETS": "error", "GlobalTheta": "error", "GlobalCroston": "error"},
    }

    # 3. infinite_value: a fresh trended draw with one +inf value.
    vals = make_trended_panel(rng)
    vals[0, 5] = np.inf
    cases["infinite_value"] = {
        "n_series": N_SERIES,
        "series_len": SERIES_LEN,
        "values": vals,
        "expect": {"GlobalETS": "error", "GlobalTheta": "error", "GlobalCroston": "error"},
    }

    # 4. intermittent_interior_gaps: intermittent-demand panel (mostly
    # zeros, occasional Poisson counts) with two interior NaN gaps.
    vals = np.where(rng.random((N_SERIES, SERIES_LEN)) < 0.35, rng.poisson(4, (N_SERIES, SERIES_LEN)) + 1, 0).astype(
        float
    )
    non_zero_counts = (vals != 0).sum(axis=1)
    assert (non_zero_counts >= 3).all(), (
        f"intermittent_interior_gaps: every series must keep >= 3 non-zero "
        f"observations, got {non_zero_counts.tolist()}"
    )
    vals[0, 7] = np.nan
    vals[2, 13] = np.nan
    cases["intermittent_interior_gaps"] = {
        "n_series": N_SERIES,
        "series_len": SERIES_LEN,
        "values": vals,
        "expect": {"GlobalCroston": "finite"},
    }

    payload = {
        "tolerance": TOLERANCE,
        "parameters": {
            "horizon": 6,
            "seasonal_period": 0,
            "layout": "values[s][t], flattened series-major for forecast_panel_impl",
            "encoding": 'string "NaN" = missing, "Infinity" = +inf',
        },
        "cases": cases,
    }

    write_fixture(
        family="panel_nan_guard",
        filename="cases.json",
        generator=generator_path(__file__),
        versions=tool_versions("numpy"),
        payload=payload,
    )


if __name__ == "__main__":
    main()
