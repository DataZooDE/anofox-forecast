"""scipy ``find_peaks``/``peak_prominences`` reference generator (family
`scipy_peaks`).

Writes `validation/reference/fixtures/scipy_peaks/cases.json`: for a fixed
set of series (an audited random walk, a seasonal sine with/without
height/distance/prominence filters, hand-picked plateau/edge/tie cases, and
degenerate constant/monotonic/tiny series), the exact peak indices/heights
scipy 1.18.1's `scipy.signal.find_peaks` returns for the case's params, plus
`scipy.signal.peak_prominences(x, peaks, wlen=None)` for EVERY case -- so a
prominence value exists for every returned peak even when no `prominence`
filter was requested.

Phase 15 plan 15-02 (DET-01, D-05) pins
`anofox_fcst_core::peaks::{local_maxima,peak_prominences,find_peaks,detect_peaks}`
against this fixture -- replacing fdars-core's own derivative
zero-crossing peak detector (audit: 51 found vs scipy 66, only 31 matching;
plateaus got prominence 0; minima were reported).

Run via: uv run --locked --project validation/reference/python python \
  validation/reference/python/gen_scipy_peaks_reference.py
"""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np
from scipy.signal import find_peaks, peak_prominences

from fixture_io import generator_path, tool_versions, write_fixture

TOLERANCE = {
    "prominence_abs": 1e-12,
    "height_abs": 0.0,
}

# validation/reference/python/gen_scipy_peaks_reference.py -> parents[1] = reference
REFERENCE_DIR = Path(__file__).resolve().parents[1]


def load_rw150() -> list[float]:
    """The `audit_rw150` series from `r_stl_strength/cases.json` -- the same
    150-point random walk the Phase 15 audit used to find 51/66 peaks
    matching scipy (31 correct). Reused here (not regenerated) so this
    fixture's `rw150` case is directly comparable to that audit finding."""
    fx_path = REFERENCE_DIR / "fixtures" / "r_stl_strength" / "cases.json"
    doc = json.loads(fx_path.read_text())
    return [float(v) for v in doc["cases"]["audit_rw150"]["values"]]


def make_sine240() -> list[float]:
    t = np.arange(240, dtype=float)
    sine = 5 * np.sin(2 * np.pi * t / 24) + np.random.default_rng(15021).normal(
        0, 1, 240
    )
    return (np.round(sine, 4) + 0.0).tolist()


def make_cases() -> list[dict]:
    sine240 = make_sine240()
    none_params = {"height": None, "distance": None, "prominence": None}

    cases: list[dict] = [
        {"name": "rw150", "x": load_rw150(), "params": dict(none_params)},
        {"name": "sine240", "x": sine240, "params": dict(none_params)},
        {
            "name": "sine240_distance8",
            "x": sine240,
            "params": {"height": None, "distance": 8.0, "prominence": None},
        },
        {
            "name": "sine240_prominence2",
            "x": sine240,
            "params": {"height": None, "distance": None, "prominence": 2.0},
        },
        {
            "name": "sine240_height3",
            "x": sine240,
            "params": {"height": 3.0, "distance": None, "prominence": None},
        },
        {
            "name": "sine240_combined",
            "x": sine240,
            "params": {"height": 1.0, "distance": 8.0, "prominence": 2.0},
        },
        {
            "name": "plateaus",
            "x": [0.0, 1.0, 1.0, 1.0, 0.0, 2.0, 2.0, 0.0, 3.0, 3.0, 3.0, 3.0, 1.0, 0.0],
            "params": dict(none_params),
        },
        {
            "name": "edge_plateaus_a",
            "x": [1.0, 1.0, 0.0, 2.0, 2.0],
            "params": dict(none_params),
        },
        {
            "name": "edge_plateaus_b",
            "x": [0.0, 1.0, 1.0],
            "params": dict(none_params),
        },
        {
            "name": "issue59_a",
            "x": [1.0, 3.0, 2.0, 5.0, 1.0, 4.0, 2.0],
            "params": dict(none_params),
        },
        {
            "name": "issue59_b",
            "x": [0.0, 0.0, 10.0, 0.0, 0.0, 0.0, 10.0, 0.0, 0.0],
            "params": dict(none_params),
        },
        {
            "name": "ties_distance",
            "x": [0.0, 5.0, 0.0, 5.0, 0.0, 3.0, 0.0],
            "params": {"height": None, "distance": 3.0, "prominence": None},
        },
        {
            "name": "constant20",
            "x": [2.0] * 20,
            "params": dict(none_params),
        },
        {
            "name": "monotonic20",
            "x": [float(i) for i in range(20)],
            "params": dict(none_params),
        },
        {
            "name": "n3",
            "x": [1.0, 2.0, 1.0],
            "params": dict(none_params),
        },
    ]
    return cases


def assert_stable_argsort_matches_default(case_name: str, x: np.ndarray) -> None:
    """For every case that uses `distance`, assert that sorting the local
    maxima's heights with numpy's default (unstable) argsort gives the same
    order as a stable argsort. The Rust port implements distance's tie-break
    with an explicit stable ascending sort (so the rightmost of equal-height
    peaks wins, per scipy's own numpy `argsort` default being stable for
    these array sizes) -- this assertion is the generator's own proof that
    the fixture's tie-break outcome is exactly reproducible that way, not an
    artifact of numpy's default sort kind happening to differ from `scipy`'s
    on this machine/version."""
    local_maxima, _ = find_peaks(x)
    if local_maxima.size == 0:
        return
    heights = x[local_maxima]
    stable_order = np.argsort(heights, kind="stable")
    default_order = np.argsort(heights)
    if not np.array_equal(stable_order, default_order):
        raise AssertionError(
            f"case {case_name!r}: stable argsort of local-maxima heights "
            f"{heights.tolist()!r} differs from numpy's default argsort "
            f"({stable_order.tolist()!r} vs {default_order.tolist()!r}) -- "
            "the distance tie-break assumption (rightmost of equal heights "
            "wins, via a stable ascending sort) is not verifiable against "
            "this fixture; choose a different series or confirm scipy's own "
            "distance behaviour directly before trusting this case."
        )


def fit_case(case: dict) -> dict:
    name = case["name"]
    x = np.array(case["x"], dtype=float)
    params = case["params"]

    kwargs: dict = {}
    if params.get("height") is not None:
        kwargs["height"] = params["height"]
    if params.get("distance") is not None:
        kwargs["distance"] = params["distance"]
    if params.get("prominence") is not None:
        kwargs["prominence"] = params["prominence"]

    if params.get("distance") is not None:
        assert_stable_argsort_matches_default(name, x)

    peaks, _props = find_peaks(x, **kwargs)
    if peaks.size > 0:
        prominences = peak_prominences(x, peaks, wlen=None)[0]
    else:
        prominences = np.array([], dtype=float)

    return {
        "name": name,
        "n": int(x.shape[0]),
        "x": x,
        "params": {
            "height": params.get("height"),
            "distance": params.get("distance"),
            "prominence": params.get("prominence"),
        },
        "expected": {
            "peaks": peaks.astype(int),
            "peak_heights": x[peaks] if peaks.size else np.array([], dtype=float),
            "prominences": prominences,
        },
    }


def main() -> None:
    cases = [fit_case(c) for c in make_cases()]

    write_fixture(
        family="scipy_peaks",
        filename="cases.json",
        generator=generator_path(__file__),
        versions=tool_versions("numpy", "scipy"),
        payload={
            "tolerance": TOLERANCE,
            "cases": cases,
        },
    )


if __name__ == "__main__":
    main()
