"""Independent numpy reference implementation of the residual-resampling
bootstrap construction (family `bootstrap`, file `lognormal_cases.json`).

Phase 16 plan 16-03 (D-10, G2): the pre-Phase-16 `bootstrap_intervals`/
`bootstrap_quantiles` passed the residuals as the upstream `fit(forecasts,
actuals)` call's `actuals` argument (and zeros as `forecasts`), which stores
`0 - residual = -residual` -- mirroring every simulated path across the
point forecast. This generator reproduces the CORRECT construction
(`simulated = point + cumsum(resampled residuals)`) independently in numpy,
so the Rust fix can be checked against ground truth rather than against its
own (possibly still-wrong) output.

Construction: for `n_sim` paths, draw `horizon` residual indices uniformly
at random (with replacement) per path, take the per-path cumulative sum,
and add it to the point forecast at each step. Per step, sort the `n_sim`
simulated values and take the order statistic at index
`floor(q * n_sim)` (clamped to `n_sim - 1`) for `q` in `{alpha, 1 - alpha}`
with `alpha = (1 - coverage) / 2` -- matching
`anofox_forecast::postprocess::bootstrap::{simulate_paths,extract_quantiles}`
exactly (upstream `BootstrapPredictor::predict`).

Run via: uv run --locked --project validation/reference/python python \
  validation/reference/python/gen_bootstrap_reference.py
"""

from __future__ import annotations

import numpy as np

from fixture_io import generator_path, tool_versions, write_fixture

# Monte Carlo sampling noise at 200,000 paths (ground truth) vs the 20,000
# paths the Rust/SQL reference tests use, relative to the ground-truth
# interval width. The mirror-anchor case is exact (its quantile indices sit
# deep inside a long run of identical discrete values, so the order
# statistic is unaffected by sampling noise at ANY path count) -- this
# tolerance only bounds the continuous lognormal cases. Measured
# empirically (both across independent numpy re-simulations at 20k/50k/
# 100k paths AND the actual Rust StdRng run at the fixture's pinned seed):
# the shortest-horizon step's width is narrow enough (residuals rounded to
# 4 decimals, only 5 cumulative draws) that relative sampling error reaches
# ~8.9% even at 100,000 paths -- a quantization/clustering effect of the
# discrete draws near that quantile's order-statistic index, not noise that
# keeps shrinking with more paths. 0.12 covers the observed worst case
# (~0.089) with margin while still catching a real sign/construction bug,
# which would miss by closer to 100% (the wrong tail entirely), not ~9%.
TOLERANCE = {"bound_rel_width": 0.12}

N_SIM = 200_000
SIM_SEED = 16052


def simulate_bootstrap(
    residuals: list[float], point: list[float], coverage: float, n_sim: int = N_SIM, seed: int = SIM_SEED
) -> dict:
    """Independent numpy simulation of the upstream bootstrap construction.

    Returns `lower`/`upper` (length `len(point)`) and `tail_ratio` =
    `(upper - point) / (point - lower)` per step (>1 means the upper tail is
    longer than the lower tail -- the right-skew signature D-10 checks for).
    """
    rng = np.random.default_rng(seed)
    res = np.asarray(residuals, dtype=np.float64)
    pt = np.asarray(point, dtype=np.float64)
    horizon = pt.shape[0]
    n_res = res.shape[0]

    idx = rng.integers(0, n_res, size=(n_sim, horizon))
    drawn = res[idx]
    cumulative = np.cumsum(drawn, axis=1)
    paths = pt[np.newaxis, :] + cumulative

    alpha = (1.0 - coverage) / 2.0
    q_lower = alpha
    q_upper = 1.0 - alpha

    lower: list[float] = []
    upper: list[float] = []
    tail_ratio: list[float] = []
    for h in range(horizon):
        col = np.sort(paths[:, h])
        lo_idx = min(int(np.floor(q_lower * n_sim)), n_sim - 1)
        hi_idx = min(int(np.floor(q_upper * n_sim)), n_sim - 1)
        lo = float(col[lo_idx])
        hi = float(col[hi_idx])
        lower.append(lo)
        upper.append(hi)
        denom = pt[h] - lo
        tail_ratio.append(float((hi - pt[h]) / denom) if denom != 0.0 else float("inf"))

    return {"lower": lower, "upper": upper, "tail_ratio": tail_ratio}


def build_mirror_anchor() -> dict:
    """9 zeros + one 10.0 (P(drawn 10) = 0.1): discrete residuals make the
    0.05/0.95 order statistics land exactly on 0.0/10.0 regardless of Monte
    Carlo noise (the quantile index sits well inside a long run of
    identical values on both sides), so lower/upper are EXACT, not just
    within tolerance -- 100.0 / 110.0, never mirrored to 90.0 / 100.0."""
    residuals = [0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 10.0]
    point = [100.0]
    coverage = 0.9
    sim = simulate_bootstrap(residuals, point, coverage, n_sim=2000, seed=42)
    return {
        "residuals": residuals,
        "point": point,
        "coverage": coverage,
        "n_paths": 2000,
        "seed": 42,
        **sim,
    }


def build_lognormal_right() -> dict:
    """60 right-skewed residuals centred at EXACTLY 0 sample mean:
    `lognormal(0, 0.8)` has population mean `exp(0.32)`, subtracted first so
    the distribution keeps its long right tail; a second subtraction of the
    resulting SAMPLE mean (not just the population mean) removes the
    residual finite-sample drift that would otherwise bias the point
    forecast's position within the cumulative (5-step) distribution and can
    flip the tail-side comparison at later steps even though the underlying
    distribution is still right-skewed (verified: the population-mean-only
    centring leaves a -0.21 sample-mean drift here, which a 5-step
    cumulative sum amplifies enough to invert `tail_ratio` at step 5)."""
    rng = np.random.default_rng(16051)
    raw = np.round(rng.lognormal(0.0, 0.8, 60) - np.exp(0.32), 4)
    residuals = np.round(raw - raw.mean(), 4).tolist()
    point = [100.0] * 5
    coverage = 0.9
    sim = simulate_bootstrap(residuals, point, coverage)
    return {
        "residuals": residuals,
        "point": point,
        "coverage": coverage,
        "n_paths": 20000,
        "seed": 42,
        **sim,
    }


def build_lognormal_left(right: dict) -> dict:
    """The mirror image of `lognormal_right`: negating every residual must
    flip the tail to the lower side -- `lower`/`upper` swap roles (up to
    sign) and `tail_ratio` inverts."""
    residuals = [-r for r in right["residuals"]]
    point = right["point"]
    coverage = right["coverage"]
    sim = simulate_bootstrap(residuals, point, coverage)
    return {
        "residuals": residuals,
        "point": point,
        "coverage": coverage,
        "n_paths": right["n_paths"],
        "seed": right["seed"],
        **sim,
    }


def main() -> None:
    lognormal_right = build_lognormal_right()
    payload = {
        "mirror_anchor": build_mirror_anchor(),
        "lognormal_right": lognormal_right,
        "lognormal_left": build_lognormal_left(lognormal_right),
        "tolerance": TOLERANCE,
    }
    out_path = write_fixture(
        family="bootstrap",
        filename="lognormal_cases.json",
        generator=generator_path(__file__),
        versions=tool_versions("numpy"),
        payload=payload,
    )
    print(f"Wrote {out_path}")


if __name__ == "__main__":
    main()
