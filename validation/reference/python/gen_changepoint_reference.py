"""Independent numpy references for BOCPD (Adams & MacKay, 2007) and PELT /
exact optimal-partitioning changepoint detection (family `changepoints`).

Phase 15 plan 15-03 (DET-02, DET-03; D-06, D-07) pins
`anofox_fcst_core::changepoint::{robust_noise_scale,bocpd_trace,detect_changepoints_bocpd,detect_changepoints}`
against this fixture. Neither reference implementation calls a library
"changepoint" function -- both are written directly from the cited equations
(BOCPD: Adams & MacKay 2007; PELT reference: exact optimal partitioning, the
same recursion PELT itself prunes), per D-06/D-07's own instruction that the
reference must be independent of the Rust implementation, not a translation
of it. R `changepoint` and Python `ruptures` are not installed in this
project's environment (confirmed in 15-RESEARCH.md), hence the hand-written
exact DP rather than either library.

Writes two fixtures under `validation/reference/fixtures/changepoints/`:
  - `bocpd_cases.json`  (Task 1)
  - `pelt_cases.json`   (Task 2, this file's `opt_partition` exact-DP reference)

Run via: uv run --locked --project validation/reference/python python \
  validation/reference/python/gen_changepoint_reference.py
"""

from __future__ import annotations

from typing import Callable

import numpy as np
from scipy.special import gammaln

from fixture_io import generator_path, tool_versions, write_fixture

# ---------------------------------------------------------------------------
# Shared: robust noise scale (D-06, D-07) -- MAD of first differences,
# rescaled to a Normal-equivalent sigma, with a documented fallback chain.
# ---------------------------------------------------------------------------

MAD_TO_SIGMA = 1.482602218505602  # 1 / Phi^-1(0.75), the usual MAD->sigma constant


def robust_noise_scale(x: np.ndarray) -> float:
    d = np.diff(x)
    med_d = np.median(d)
    mad = np.median(np.abs(d - med_d))
    s = MAD_TO_SIGMA * mad / np.sqrt(2.0)
    if not (s > 0 and np.isfinite(s)):
        s = np.std(d, ddof=0) / np.sqrt(2.0)
    if not (s > 0 and np.isfinite(s)):
        s = 1.0
    return float(s)


# ---------------------------------------------------------------------------
# BOCPD (Adams & MacKay 2007): normalised Student-t predictive, Normal-Gamma
# posterior update using the SAMPLE mean, log-domain normalisation, and
# run-length truncation at 500 that merges the tail mass instead of
# dropping it.
# ---------------------------------------------------------------------------

MU0, KAPPA0, ALPHA0, BETA0 = 0.0, 1.0, 1.0, 1.0
MAX_RUN_LENGTH = 500


def bocpd(x: np.ndarray, hazard_lambda: float) -> dict:
    n = len(x)
    sigma_hat = robust_noise_scale(x)
    loc = float(np.median(x))
    z = (x - loc) / sigma_hat
    h = 1.0 / max(hazard_lambda, 1.0)

    # Run-length state: P sums to 1 over run lengths r = 0..len(P)-1.
    p = np.array([1.0])
    cnt = np.array([0.0])
    s1 = np.array([0.0])
    s2 = np.array([0.0])

    prob = np.zeros(n)
    map_run_length = np.zeros(n, dtype=int)
    is_changepoint = np.zeros(n, dtype=bool)

    prev_m: int | None = None
    for t in range(n):
        zt = z[t]
        n_r = cnt
        safe_n_r = np.where(n_r > 0, n_r, 1.0)
        xbar = np.where(n_r > 0, s1 / safe_n_r, 0.0)
        ss = np.maximum(s2 - np.where(n_r > 0, s1**2 / safe_n_r, 0.0), 0.0)

        kappa_n = KAPPA0 + n_r
        mu_n = s1 / kappa_n  # (kappa0*mu0 + s1) / kappa_n, mu0 = 0
        alpha_n = ALPHA0 + n_r / 2.0
        beta_n = BETA0 + ss / 2.0 + KAPPA0 * n_r * (xbar - MU0) ** 2 / (2.0 * kappa_n)

        nu = 2.0 * alpha_n
        scale = np.sqrt(beta_n * (kappa_n + 1.0) / (alpha_n * kappa_n))
        u = (zt - mu_n) / scale
        logpred = (
            gammaln((nu + 1.0) / 2.0)
            - gammaln(nu / 2.0)
            - 0.5 * np.log(nu * np.pi)
            - np.log(scale)
            - (nu + 1.0) / 2.0 * np.log(1.0 + u**2 / nu)
        )

        logw = np.log(p) + logpred
        m = np.max(logw)
        w = np.exp(logw - m)

        g = np.zeros(len(p) + 1)
        g[0] = h * np.sum(w)
        g[1:] = (1.0 - h) * w
        p_next = g / np.sum(g)

        cnt_next = np.concatenate(([0.0], n_r + 1.0))
        s1_next = np.concatenate(([0.0], s1 + zt))
        s2_next = np.concatenate(([0.0], s2 + zt**2))

        if len(p_next) > MAX_RUN_LENGTH:
            tail = np.sum(p_next[MAX_RUN_LENGTH - 1 :])
            p_next = p_next[:MAX_RUN_LENGTH].copy()
            p_next[MAX_RUN_LENGTH - 1] = tail
            cnt_next = cnt_next[:MAX_RUN_LENGTH]
            s1_next = s1_next[:MAX_RUN_LENGTH]
            s2_next = s2_next[:MAX_RUN_LENGTH]
            p_next = p_next / np.sum(p_next)

        m_t = int(np.argmax(p_next[1:]) + 1) if len(p_next) > 1 else 0
        prob[t] = p_next[1] if len(p_next) > 1 else 0.0
        map_run_length[t] = m_t

        if prev_m is not None and m_t < prev_m:
            i = t - m_t + 1
            if 1 <= i <= n - 1:
                is_changepoint[i] = True
        prev_m = m_t

        p, cnt, s1, s2 = p_next, cnt_next, s1_next, s2_next

    changepoints = sorted(int(i) for i in np.nonzero(is_changepoint)[0])

    return {
        "sigma_hat": sigma_hat,
        "loc": loc,
        "prob": prob,
        "map_run_length": map_run_length,
        "is_changepoint": is_changepoint,
        "changepoints": changepoints,
    }


# ---------------------------------------------------------------------------
# BOCPD cases
# ---------------------------------------------------------------------------


def issue287_series(base: float, step_at_50: bool) -> np.ndarray:
    """The #287 panel's exact SQL formula (test/sql/correctness/
    detection_decomposition.test): `base + 0.3*i + 5*sin(2*pi*i/7) +
    (step_at_50 and i>=50 ? 15 : 0) + 1.6*cos(i*0.9) + 0.7*sin(i*1.7+base)`
    for `i` in `0..83` (84 days). `base=20.0`/no step -> series A,
    `base=40.0`/step -> series B, `base=60.0`/no step -> series C."""
    i = np.arange(84, dtype=float)
    step = np.where(i >= 50, 15.0, 0.0) if step_at_50 else np.zeros_like(i)
    return (
        base
        + 0.3 * i
        + 5.0 * np.sin(2.0 * np.pi * i / 7.0)
        + step
        + 1.6 * np.cos(i * 0.9)
        + 0.7 * np.sin(i * 1.7 + base)
    )


def make_bocpd_cases() -> list[dict]:
    rng_step3 = np.random.default_rng(15101)
    step3 = rng_step3.normal(0, 1, 200)
    step3[100:] += 3.0

    rng_noise = np.random.default_rng(15201)
    noise = rng_noise.normal(0, 1, 200)

    gross_step = np.array([100.0] * 12 + [10.0] * 12)

    rng_two = np.random.default_rng(15401)
    two_step = rng_two.normal(0, 1, 300)
    two_step[100:] += 3.0
    two_step[200:] -= 3.0

    rng_long = np.random.default_rng(15301)
    long_series = rng_long.normal(0, 1, 1500)
    long_series[700:] += 3.0

    constant = np.array([5.0] * 30)
    short = np.array([1.0, 2.0, 3.0])
    step3_scaled = step3 * 100.0 + 7.0

    # Phase 20 plan 20-06 Task 3 (#287, BUG-06, D2-ii): the #287 panel's
    # exact three series, hazard_lambda 250 (the SQL default).
    issue287_a = issue287_series(20.0, False)
    issue287_b = issue287_series(40.0, True)
    issue287_c = issue287_series(60.0, False)

    raw_cases = [
        ("step3_n200_h250", step3, 250.0),
        ("step3_n200_h50", step3, 50.0),
        ("step3_n200_h1000", step3, 1000.0),
        ("noise_n200", noise, 250.0),
        ("gross_step_n24", gross_step, 250.0),
        ("two_step_n300", two_step, 250.0),
        ("long_n1500", long_series, 250.0),
        ("constant_n30", constant, 250.0),
        ("short_n3", short, 250.0),
        ("step3_scaled", step3_scaled, 250.0),
        ("issue287_A", issue287_a, 250.0),
        ("issue287_B", issue287_b, 250.0),
        ("issue287_C", issue287_c, 250.0),
    ]

    cases = []
    base_changepoints = None
    for name, x, hazard in raw_cases:
        x = np.round(x, 4) + 0.0
        result = bocpd(x, hazard)
        if name == "step3_n200_h250":
            base_changepoints = result["changepoints"]
        if name == "step3_scaled":
            assert result["changepoints"] == base_changepoints, (
                f"step3_scaled flags {result['changepoints']} differ from "
                f"step3_n200_h250's {base_changepoints} -- scale invariance broken"
            )
        expected = {
            "sigma_hat": result["sigma_hat"],
            "loc": result["loc"],
            "map_run_length": result["map_run_length"].tolist(),
            "prob": result["prob"].tolist(),
            "is_changepoint": result["is_changepoint"].tolist(),
            "changepoints": result["changepoints"],
        }
        if name.startswith("issue287_"):
            # D2-ii (user decision, 20-CONTEXT.md decisions_addendum_2):
            # is_changepoint = (prob >= threshold) for every index AFTER the
            # first; index 0 is never included even if prob[0] >= threshold.
            expected["expected_threshold_0_5"] = [
                i for i in range(1, len(x)) if result["prob"][i] >= 0.5
            ]
        cases.append(
            {
                "name": name,
                "x": x,
                "hazard_lambda": hazard,
                "expected": expected,
            }
        )

    # Behaviour anchors, asserted here so a future regeneration fails loudly
    # if the implementation drifts from 15-RESEARCH.md's prototype anchors.
    anchors = {c["name"]: c for c in cases}
    assert anchors["step3_n200_h250"]["expected"]["changepoints"] == [100]
    assert anchors["noise_n200"]["expected"]["changepoints"] == []
    assert anchors["gross_step_n24"]["expected"]["changepoints"] == [12]
    assert anchors["two_step_n300"]["expected"]["changepoints"] == [100, 199]
    assert anchors["long_n1500"]["expected"]["changepoints"] == [700, 1418]
    assert anchors["constant_n30"]["expected"]["changepoints"] == []

    # #287 anchors (BUG-06, D2-ii): series B's single +15 level shift at
    # index 50 (2024-02-20 in the SQL fixture) is the only index across all
    # three series whose probability crosses 0.5.
    assert anchors["issue287_A"]["expected"]["expected_threshold_0_5"] == []
    assert anchors["issue287_B"]["expected"]["expected_threshold_0_5"] == [50]
    assert anchors["issue287_C"]["expected"]["expected_threshold_0_5"] == []

    return cases


def write_bocpd_fixture() -> None:
    cases = make_bocpd_cases()
    write_fixture(
        family="changepoints",
        filename="bocpd_cases.json",
        generator=generator_path(__file__),
        versions=tool_versions("numpy", "scipy"),
        payload={
            "tolerance": {"prob_abs": 1e-10, "sigma_rel": 1e-12},
            "cases": cases,
        },
    )


# ---------------------------------------------------------------------------
# PELT reference: exact optimal partitioning (no pruning) over normalised
# costs, so PELT itself (with pruning) can be checked for exact agreement.
# ---------------------------------------------------------------------------


def cost_l2(z: np.ndarray, a: int, b: int) -> float:
    seg = z[a:b]
    mean = seg.mean()
    return float(((seg - mean) ** 2).sum())


def cost_l1(z: np.ndarray, a: int, b: int) -> float:
    seg = z[a:b]
    med = np.median(seg)
    return float(2.0 * np.sqrt(2.0) * np.abs(seg - med).sum())


def cost_normal(z: np.ndarray, a: int, b: int) -> float:
    seg = z[a:b]
    n = len(seg)
    mean = seg.mean()
    var_mle = float(((seg - mean) ** 2).sum() / n)
    return float(n * (1.0 + np.log(var_mle + 1e-8)))


COST_FNS: dict[str, Callable[[np.ndarray, int, int], float]] = {
    "L1": cost_l1,
    "L2": cost_l2,
    "Normal": cost_normal,
}


def default_penalty(cost: str, n: int) -> float:
    if cost == "Normal":
        return 3.0 * np.log(n)
    return 2.0 * np.log(n)


def effective_min_size(cost: str, min_size: int) -> int:
    base = max(min_size, 1)
    if cost == "Normal":
        return max(base, 2)
    return base


def opt_partition(
    x: np.ndarray, cost: str, min_size: int, penalty: float | None
) -> dict:
    """Exact optimal partitioning: F(t) = min over admissible tau < t of
    F(tau) + C(tau, t) + beta, scanning EVERY admissible tau (no pruning).
    This is the ground truth PELT (with pruning) must reproduce exactly."""
    n = len(x)
    sigma_hat = robust_noise_scale(x)
    z = x / sigma_hat
    m = effective_min_size(cost, min_size)
    cost_fn = COST_FNS[cost]

    if n < 2 * m:
        return {
            "sigma_hat": sigma_hat,
            "changepoints": [],
            "cost": cost_fn(z, 0, n) if n > 0 else 0.0,
        }

    beta = default_penalty(cost, n) if penalty is None else penalty

    f = [float("inf")] * (n + 1)
    back = [0] * (n + 1)
    f[0] = -beta

    for t in range(m, n + 1):
        candidates = [tau for tau in range(0, t - m + 1) if tau == 0 or tau >= m]
        best_f = float("inf")
        best_tau = 0
        for tau in candidates:
            c = f[tau] + cost_fn(z, tau, t) + beta
            if c < best_f:
                best_f = c
                best_tau = tau
        f[t] = best_f
        back[t] = best_tau

    changepoints: list[int] = []
    idx = n
    while idx > 0:
        tau = back[idx]
        if tau > 0:
            changepoints.append(tau)
        idx = tau
    changepoints.reverse()

    return {"sigma_hat": sigma_hat, "changepoints": changepoints, "cost": f[n]}


def make_pelt_cases() -> list[dict]:
    rng_mean3 = np.random.default_rng(15501)
    mean3 = np.concatenate(
        [
            np.zeros(50),
            np.full(50, 2.5),
            np.full(50, -1.0),
        ]
    ) + rng_mean3.normal(0, 1, 150)

    rng_var2 = np.random.default_rng(15502)
    var2 = np.concatenate([rng_var2.normal(0, 1, 75), rng_var2.normal(0, 3, 75)])

    noiseless_step = np.concatenate([np.zeros(30), np.full(30, 10.0)])

    rng_noise100 = np.random.default_rng(15503)
    noise100 = rng_noise100.normal(0, 1, 100)

    constant40 = np.full(40, 3.0)

    rng_outlier = np.random.default_rng(15504)
    outlier = rng_outlier.normal(0, 1, 80)
    outlier[20] += 8.0
    outlier[45] += 8.0
    outlier[70] += 8.0

    cases: list[dict] = []

    def add_case(name: str, x: np.ndarray, cost: str, min_size: int, penalty):
        x = np.round(x, 4) + 0.0
        result = opt_partition(x, cost, min_size, penalty)
        cases.append(
            {
                "name": name,
                "x": x,
                "cost": cost,
                "min_size": min_size,
                "penalty": penalty,
                "expected": {
                    "sigma_hat": result["sigma_hat"],
                    "changepoints": result["changepoints"],
                    "cost": result["cost"],
                },
            }
        )

    for cost in ("L1", "L2", "Normal"):
        for min_size in (1, 5):
            add_case(f"mean3_n150_{cost}_m{min_size}", mean3, cost, min_size, None)

    add_case("var2_n150_Normal_m2", var2, "Normal", 2, None)
    add_case("var2_n150_L2_m2", var2, "L2", 2, None)

    add_case("noiseless_step_n60_L2_m1", noiseless_step, "L2", 1, None)
    add_case("noiseless_step_n60_L2_m5", noiseless_step, "L2", 5, None)

    add_case("noise_n100_L2_m2", noise100, "L2", 2, None)

    for cost in ("L1", "L2", "Normal"):
        add_case(f"constant_n40_{cost}", constant40, cost, 2, None)

    add_case("outlier_n80_L1", outlier, "L1", 2, None)
    add_case("outlier_n80_L2", outlier, "L2", 2, None)

    add_case("penalty_15", mean3, "L2", 5, 15.0)

    # Scale/shift invariance base case (the generator asserts it here too;
    # the Rust test re-asserts via `pelt_is_scale_and_shift_invariant`).
    base_result = opt_partition(mean3, "L2", 5, None)
    scaled_result = opt_partition(mean3 * 100.0, "L2", 5, None)
    shifted_result = opt_partition(mean3 + 1e6, "L2", 5, None)
    assert scaled_result["changepoints"] == base_result["changepoints"], (
        "mean3_x100 changepoints differ from mean3_n150 base -- scale "
        "invariance broken in the numpy reference itself"
    )
    assert shifted_result["changepoints"] == base_result["changepoints"], (
        "mean3_shift changepoints differ from mean3_n150 base -- shift "
        "invariance broken in the numpy reference itself"
    )
    add_case("mean3_x100", mean3 * 100.0, "L2", 5, None)
    add_case("mean3_shift", mean3 + 1e6, "L2", 5, None)

    cost_cycle = ["L1", "L2", "Normal"]
    min_size_cycle = [1, 2, 3]
    for k in range(30):
        n = 8 + 2 * (k % 27)
        rng_k = np.random.default_rng(15600 + k)
        n_segments = 1 + (k % 4)
        seg_lens = [n // n_segments] * n_segments
        seg_lens[-1] += n - sum(seg_lens)
        means = rng_k.uniform(-5, 5, n_segments)
        series = np.concatenate(
            [np.full(seg_len, mean) for seg_len, mean in zip(seg_lens, means)]
        )
        series = series + rng_k.normal(0, 1, n)
        add_case(
            f"random_small_{k:02d}",
            series,
            cost_cycle[k % 3],
            min_size_cycle[k % 3],
            None,
        )

    return cases


def write_pelt_fixture() -> None:
    cases = make_pelt_cases()
    write_fixture(
        family="changepoints",
        filename="pelt_cases.json",
        generator=generator_path(__file__),
        versions=tool_versions("numpy", "scipy"),
        payload={
            "tolerance": {"cost_rel": 1e-9, "sigma_rel": 1e-12},
            "cases": cases,
        },
    )


def main() -> None:
    write_bocpd_fixture()
    write_pelt_fixture()


if __name__ == "__main__":
    main()
