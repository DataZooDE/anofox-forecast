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
  - `bocpd_cases.json`  (this file, Task 1)
  - `pelt_cases.json`   (this file, Task 2 -- added in a later commit)

Run via: uv run --locked --project validation/reference/python python \
  validation/reference/python/gen_changepoint_reference.py
"""

from __future__ import annotations

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
        cases.append(
            {
                "name": name,
                "x": x,
                "hazard_lambda": hazard,
                "expected": {
                    "sigma_hat": result["sigma_hat"],
                    "loc": result["loc"],
                    "map_run_length": result["map_run_length"].tolist(),
                    "prob": result["prob"].tolist(),
                    "is_changepoint": result["is_changepoint"].tolist(),
                    "changepoints": result["changepoints"],
                },
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


def main() -> None:
    write_bocpd_fixture()


if __name__ == "__main__":
    main()
