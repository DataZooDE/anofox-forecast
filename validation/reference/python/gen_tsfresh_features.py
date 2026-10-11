"""Independent tsfresh/numpy/scipy reference implementation of the 117 names in
`anofox_fcst_core::features::list_features()` (family `tsfresh_features`).

Reads `validation/reference/fixtures/tsfresh_features/mapping.json` (the D-01
parity contract) and, for every row, computes the reference value by calling
`tsfresh.feature_extraction.feature_calculators.<calculator>` DIRECTLY with the
row's exact pinned params -- NEVER via `tsfresh.extract_features()`'s
settings-driven convenience API, which silently uses the wrong default
parameter grid for several of this project's exact choices (agg_linear_trend
chunk_len, approximate_entropy r=0.2, ...; see 14-RESEARCH.md "Fixture
Generation Methodology" / Pitfall 2). Rows with `reference: "numpy/scipy"` have
no tsfresh counterpart and are computed via a hand-written formula in the
FORMULAS dict below (D-02).

Writes one fixture per series under `validation/reference/fixtures/tsfresh_features/`:
`{family, generator, tool_versions, tolerance, series: {name, n, recipe, seed,
source, values}, expected: {feature_name: value, ...}}` (mapping order).

Run via:
  uv run --locked --project validation/reference/python python \
    validation/reference/python/gen_tsfresh_features.py
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Callable, Optional

import numpy as np
import pandas as pd
from scipy import stats as scipy_stats
from tsfresh.feature_extraction import feature_calculators as fc

from fixture_io import FIXTURES, generator_path, tool_versions, write_fixture

MAPPING_PATH = FIXTURES / "tsfresh_features" / "mapping.json"
MOMENTS_FIXTURE_PATH = FIXTURES / "moments" / "scipy_moments.json"
MOMENTS_FIXTURE_REL = "validation/reference/fixtures/moments/scipy_moments.json"

# chunk_len tokens the mapping file may use in place of a literal integer
# (resolved per-series, since the project's own chunk_len depends on n).
CHUNK_LEN_TOKENS: dict[str, Callable[[int], int]] = {
    "n_div_10_min_2": lambda n: max(n // 10, 2),
}


# ---------------------------------------------------------------------------
# Series construction (D-09 + edge cases). Stochastic series are rounded to 4
# decimals with a trailing `+ 0.0` to turn any `-0.0` into `+0.0` (determinism
# across platforms/numpy versions).
# ---------------------------------------------------------------------------


def _clean(x: np.ndarray) -> np.ndarray:
    return np.round(x, 4) + 0.0


def build_series() -> dict[str, dict[str, Any]]:
    series: dict[str, dict[str, Any]] = {}

    rng = np.random.default_rng(14001)
    series["white_noise_n200"] = {
        "values": _clean(rng.normal(0, 1, 200)),
        "recipe": "np.random.default_rng(14001).normal(0, 1, 200)",
        "seed": 14001,
        "source": None,
    }

    rng = np.random.default_rng(14010)
    series["random_walk_n200"] = {
        "values": _clean(np.cumsum(rng.normal(0, 1, 200))),
        "recipe": "np.cumsum(np.random.default_rng(14010).normal(0, 1, 200))",
        "seed": 14010,
        "source": None,
    }

    rng = np.random.default_rng(14003)
    e = rng.normal(0, 1, 384)
    y = np.zeros(384)
    for t in range(384):
        y[t] = e[t] + (0.7 * y[t - 12] if t >= 12 else 0.0)
    series["seasonal_ar12_n144"] = {
        "values": _clean(y[-144:]),
        "recipe": "rng(14003), e=normal(0,1,384); y[t]=e[t]+0.7*y[t-12] (t>=12); keep last 144",
        "seed": 14003,
        "source": None,
    }

    rng = np.random.default_rng(14004)
    t_arr = np.arange(120)
    values = 10 + 0.05 * t_arr + 2 * np.sin(2 * np.pi * t_arr / 12) + rng.normal(0, 0.5, 120)
    series["trend_season_n120"] = {
        "values": _clean(values),
        "recipe": "rng(14004), t=arange(120); 10 + 0.05*t + 2*sin(2*pi*t/12) + normal(0,0.5,120)",
        "seed": 14004,
        "source": None,
    }

    rng = np.random.default_rng(14005)
    occur = rng.random(100) < 0.3
    size = rng.poisson(4.0, 100) + 1
    values = np.where(occur, size, 0).astype(float)
    series["intermittent_n100"] = {
        "values": _clean(values),
        "recipe": "rng(14005): occur=random(100)<0.3; size=poisson(4.0,100)+1; where(occur,size,0)",
        "seed": 14005,
        "source": None,
    }

    moments_fixture = json.loads(MOMENTS_FIXTURE_PATH.read_text())
    x20 = moments_fixture["vectors"]["x20"]["x"]
    series["short_n20"] = {
        "values": np.array(x20, dtype=float),
        "recipe": "audit x20 vector (ROADMAP SC4 anchor: tsfresh sample_entropy = ln 3 = 1.0986122886681098)",
        "seed": None,
        "source": MOMENTS_FIXTURE_REL,
    }

    series["constant_n30"] = {
        "values": np.full(30, 7.0),
        "recipe": "np.full(30, 7.0)",
        "seed": None,
        "source": None,
    }

    series["all_zero_n50"] = {
        "values": np.zeros(50),
        "recipe": "np.zeros(50)",
        "seed": None,
        "source": None,
    }

    rng = np.random.default_rng(14008)
    series["tiny_n8"] = {
        "values": _clean(rng.normal(0, 1, 8)),
        "recipe": "np.random.default_rng(14008).normal(0, 1, 8)",
        "seed": 14008,
        "source": None,
    }

    return series


# ---------------------------------------------------------------------------
# numpy/scipy direct-formula references (D-02) -- names with no tsfresh
# counterpart. Each formula is the exact text quoted in 14-01-PLAN.md's
# <interfaces> block / 14-RESEARCH.md.
# ---------------------------------------------------------------------------


def formula_range(x: np.ndarray) -> float:
    return float(np.max(x) - np.min(x))


def formula_percentage_above_mean(x: np.ndarray) -> float:
    n = len(x)
    return float(np.sum(x > np.mean(x)) / n)


def formula_zero_crossing_rate(x: np.ndarray) -> float:
    n = len(x)
    count = 0
    for i in range(n - 1):
        if x[i] != 0 and x[i + 1] != 0 and np.sign(x[i]) != np.sign(x[i + 1]):
            count += 1
    return float(count / max(n - 1, 1))


def formula_first_value(x: np.ndarray) -> float:
    return float(x[0])


def formula_last_value(x: np.ndarray) -> float:
    return float(x[-1])


def _number_peaks_threshold(x: np.ndarray, k: float) -> float:
    n = len(x)
    mean = np.mean(x)
    std = np.std(x)
    count = 0
    for i in range(1, n - 1):
        if x[i] > x[i - 1] and x[i] > x[i + 1] and abs(x[i] - mean) > k * std:
            count += 1
    return float(count)


def formula_number_peaks_threshold_1(x: np.ndarray) -> float:
    return _number_peaks_threshold(x, 1)


def formula_number_peaks_threshold_2(x: np.ndarray) -> float:
    return _number_peaks_threshold(x, 2)


def formula_linear_trend_r_squared(x: np.ndarray) -> float:
    n = len(x)
    result = scipy_stats.linregress(np.arange(n), x)
    return float(result.rvalue**2)


def formula_count_unique(x: np.ndarray) -> float:
    return float(np.unique(x).size)


FORMULAS: dict[str, Callable[[np.ndarray], float]] = {
    "range": formula_range,
    "percentage_above_mean": formula_percentage_above_mean,
    "zero_crossing_rate": formula_zero_crossing_rate,
    "first_value": formula_first_value,
    "last_value": formula_last_value,
    "number_peaks_threshold_1": formula_number_peaks_threshold_1,
    "number_peaks_threshold_2": formula_number_peaks_threshold_2,
    "linear_trend_r_squared": formula_linear_trend_r_squared,
    "count_unique": formula_count_unique,
}


# ---------------------------------------------------------------------------
# tsfresh calculator dispatch -- direct calls only, never the convenience API.
# ---------------------------------------------------------------------------


def resolve_params(params: dict[str, Any], n: int) -> dict[str, Any]:
    resolved: dict[str, Any] = {}
    for key, value in params.items():
        if key == "chunk_len" and isinstance(value, str):
            if value not in CHUNK_LEN_TOKENS:
                raise ValueError(f"resolve_params: unknown chunk_len token {value!r}")
            resolved[key] = CHUNK_LEN_TOKENS[value](n)
        else:
            resolved[key] = value
    return resolved


def call_tsfresh(calculator_name: str, x: np.ndarray, params: dict[str, Any], as_series: bool) -> float:
    calculator = getattr(fc, calculator_name)
    arg: Any = pd.Series(x) if as_series else np.asarray(x, dtype=float)
    if calculator.fctype == "combiner":
        result = list(calculator(arg, [params]))
        if len(result) != 1:
            raise ValueError(
                f"call_tsfresh({calculator_name}): expected exactly one (key, value) pair "
                f"from combiner call with params {params!r}, got {len(result)}"
            )
        _, value = result[0]
    else:
        value = calculator(arg, **params)
    return float(value)


def compute_expected(row: dict[str, Any], x: np.ndarray) -> float:
    reference = row["reference"]
    if reference == "tsfresh":
        params = resolve_params(row.get("params", {}), len(x))
        as_series = row.get("input") == "series"
        return call_tsfresh(row["calculator"], x, params, as_series)
    if reference == "numpy/scipy":
        formula = FORMULAS.get(row["name"])
        if formula is None:
            raise ValueError(f"compute_expected: no FORMULAS entry for numpy/scipy row {row['name']!r}")
        return formula(x)
    raise ValueError(f"compute_expected: unknown reference {reference!r} for row {row['name']!r}")


def check_tool_versions(mapping: dict[str, Any]) -> None:
    installed = tool_versions("tsfresh", "statsmodels", "numpy", "scipy", "pandas")
    for name, pinned_version in mapping["tool_versions"].items():
        installed_version = installed.get(name)
        if installed_version is None:
            continue
        if installed_version != pinned_version:
            raise SystemExit(
                f"gen_tsfresh_features.py: mapping.json pins tool_versions.{name}={pinned_version!r} "
                f"but the installed version is {installed_version!r}. If this bump is intended, "
                f"update mapping.json's tool_versions deliberately and re-review every reference "
                f"value it affects."
            )


def main() -> None:
    mapping = json.loads(MAPPING_PATH.read_text())
    check_tool_versions(mapping)

    rows = mapping["features"]
    series_map = build_series()

    for series_name, series_info in series_map.items():
        values = series_info["values"]
        expected: dict[str, Any] = {}
        for row in rows:
            expected[row["name"]] = compute_expected(row, values)

        write_fixture(
            family="tsfresh_features",
            filename=f"{series_name}.json",
            generator=generator_path(__file__),
            versions=tool_versions("numpy", "scipy", "pandas", "statsmodels", "tsfresh"),
            payload={
                "tolerance": mapping["tolerance"],
                "series": {
                    "name": series_name,
                    "n": len(values),
                    "recipe": series_info["recipe"],
                    "seed": series_info["seed"],
                    "source": series_info["source"],
                    "values": values,
                },
                "expected": expected,
            },
        )

    print(f"gen_tsfresh_features.py: wrote {len(series_map)} fixtures, {len(rows)} features each")


if __name__ == "__main__":
    main()
