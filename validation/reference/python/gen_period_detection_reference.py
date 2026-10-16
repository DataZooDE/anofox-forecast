"""scipy/statsmodels/numpy reference generator (family `period_detection`).

Writes `validation/reference/fixtures/period_detection/cases.json` with FIVE
case groups, pinning `anofox_fcst_core::periods::{detrended_periodogram,
estimate_period_fft_ts, acf, estimate_period_acf_ts, regression_period_rss,
estimate_period_regression_ts, lomb_scargle_power, lomb_scargle}` and
`anofox_fcst_core::periods::detrended_periodogram` / seasonality strength
detrending (Phase 15 plan 15-05, DET-05/DET-06, D-11/D-12):

- `periodogram_cases`: `scipy.signal.periodogram(x, fs=1, window='boxcar',
  detrend='linear', scaling='spectrum')` one-sided power, plus our own
  period-selection rule (strongest bin 3 <= k <= floor(n/2), confidence =
  peak / mean(power[1:]), NaN period for a floor(n/2) < 3 or a
  scale-relative-constant detrended series) applied in Python so the
  fixture's `period`/`confidence` are self-consistent with what the Rust
  side is REQUIRED to compute -- not scipy's own (nonexistent) period
  selection.
- `acf_cases`: `statsmodels.tsa.stattools.acf(x, nlags=max_lag, fft=False,
  adjusted=False)`, plus the same "smallest significant local maximum from
  lag 2" selection rule applied in Python.
- `regression_cases`: `numpy.linalg.lstsq` RSS for a harmonic-regression
  design (intercept + cos/sin harmonics below Nyquist) at each integer
  candidate period, plus the same "smallest period within 1e-10*SST of the
  minimum RSS" selection rule.
- `lombscargle_cases`: `scipy.signal.lombscargle(t, y - mean, 2*pi*f,
  normalize=True) * n/2` over the exact frequency grid our own
  implementation builds (`min_period`..`max_period`, `n_frequencies`
  points), plus the resulting best period.
- `strength_cases`: `scipy.signal.detrend(x, type='linear')`, pinning the
  DETRENDED values `seasonal_strength_variance`/`_spectral` now feed to
  fdars-core instead of the raw series (G19).

Run via: uv run --locked --project validation/reference/python python \
  validation/reference/python/gen_period_detection_reference.py
"""

from __future__ import annotations

import numpy as np
from scipy.signal import detrend as scipy_detrend
from scipy.signal import lombscargle as scipy_lombscargle
from scipy.signal import periodogram as scipy_periodogram
from statsmodels.tsa.stattools import acf as sm_acf

from fixture_io import generator_path, tool_versions, write_fixture

TOLERANCE = {
    "power_rel": 1e-9,
    "power_abs": 1e-12,
    "acf_abs": 1e-12,
    "rss_rel": 1e-9,
    # Absolute floor alongside rss_rel: a near-perfect fit's RSS is
    # conceptually 0, and numpy.linalg.lstsq's own floating-point
    # evaluation of that 0 is not bit-identical to the Rust QR solver's
    # (both ~1e-28 but differing in the last few digits) -- a relative-only
    # bound cannot absorb that (same rationale as numpy_detrend's
    # trend_abs, validation/reference/README.md).
    "rss_abs": 1e-20,
    "ls_rel": 1e-9,
    "detrend_abs": 1e-10,
}


# ---------------------------------------------------------------------------
# Shared: scale-relative "is constant" gate (D-13, filter::is_constant_slice)
# ---------------------------------------------------------------------------


def is_constant_slice(values: np.ndarray, tol: float = 1e-10) -> bool:
    finite = values[np.isfinite(values)]
    if finite.size < 2:
        return True
    rng = float(finite.max() - finite.min())
    scale = max(1.0, float(np.max(np.abs(finite))))
    return rng <= tol * scale


# ---------------------------------------------------------------------------
# Group 1: periodogram (FFT period, D-11, G18)
# ---------------------------------------------------------------------------


def periodogram_case(x: np.ndarray) -> dict:
    n = x.shape[0]
    _freqs, power = scipy_periodogram(
        x, fs=1.0, window="boxcar", detrend="linear", scaling="spectrum"
    )
    detrended = scipy_detrend(x, type="linear")

    max_k = n // 2
    period: float | None
    confidence: float
    if max_k < 3 or is_constant_slice(detrended):
        period = None
        confidence = 0.0
    else:
        non_dc_mean = float(np.mean(power[1:]))
        if non_dc_mean <= 0.0:
            period = None
            confidence = 0.0
        else:
            best_k = max(range(3, max_k + 1), key=lambda k: power[k])
            period = n / best_k
            confidence = float(power[best_k] / non_dc_mean)

    return {
        "n": n,
        "x": x,
        "expected": {
            "power": power,
            "period": period,
            "confidence": confidence,
        },
    }


def make_periodogram_cases() -> dict[str, dict]:
    cases: dict[str, dict] = {}

    t120 = np.arange(120, dtype=float)
    cases["trend_season_n120"] = periodogram_case(0.5 * t120 + 2.0 * np.sin(2 * np.pi * t120 / 12))

    t121 = np.arange(121, dtype=float)
    cases["trend_season_n121"] = periodogram_case(0.5 * t121 + 2.0 * np.sin(2 * np.pi * t121 / 12))

    t200 = np.arange(200, dtype=float)
    noisy = 3.0 * np.sin(2 * np.pi * t200 / 25) + np.random.default_rng(15702).normal(0, 1, 200)
    cases["seasonal_noisy_n200"] = periodogram_case(np.round(noisy, 4) + 0.0)

    noise = np.random.default_rng(15701).normal(0, 1, 128)
    cases["noise_n128"] = periodogram_case(np.round(noise, 4) + 0.0)

    t7 = np.arange(7, dtype=float)
    cases["odd7"] = periodogram_case(t7**2 + np.sin(t7))

    t50 = np.arange(50, dtype=float)
    cases["linear_n50"] = periodogram_case(2.0 + 0.3 * t50)

    cases["constant_n30"] = periodogram_case(np.full(30, 4.25, dtype=float))

    return cases


# ---------------------------------------------------------------------------
# Group 2: ACF period (D-11, G17)
# ---------------------------------------------------------------------------


def acf_case(x: np.ndarray, max_lag: int) -> dict:
    n = x.shape[0]
    with np.errstate(invalid="ignore", divide="ignore"):
        r = sm_acf(x, nlags=max_lag, fft=False, adjusted=False)

    bound = 1.96 / np.sqrt(n)
    period: float | None = None
    if max_lag >= 3:
        for k in range(2, max_lag):
            rk = r[k]
            if np.isnan(rk):
                continue
            if rk > r[k - 1] and rk >= r[k + 1] and rk > bound:
                period = float(k)
                break

    return {
        "n": n,
        "x": x,
        "max_lag": max_lag,
        "expected": {
            "acf": r,
            "period": period,
        },
    }


def make_acf_cases() -> dict[str, dict]:
    cases: dict[str, dict] = {}

    alternating = np.array([1.0, -1.0] * 20)
    cases["alternating_n40"] = acf_case(alternating, 20)

    t144 = np.arange(144, dtype=float)
    sine_noisy = np.sin(2 * np.pi * t144 / 12) + 0.5 * np.random.default_rng(15703).normal(
        0, 1, 144
    )
    cases["sine12_noisy_n144"] = acf_case(np.round(sine_noisy, 4) + 0.0, 72)

    noise = np.random.default_rng(15704).normal(0, 1, 200)
    cases["noise_n200"] = acf_case(np.round(noise, 4) + 0.0, 100)

    cases["constant_n30"] = acf_case(np.full(30, 4.25, dtype=float), 15)

    return cases


# ---------------------------------------------------------------------------
# Group 3: harmonic-regression period (D-11, G20)
# ---------------------------------------------------------------------------


def harmonic_design_matrix(t: np.ndarray, period: float, harmonics: int) -> np.ndarray:
    cols = [np.ones_like(t)]
    for k in range(1, harmonics + 1):
        two_k = 2.0 * k
        if two_k > period:
            break
        angle = 2 * np.pi * k * t / period
        cols.append(np.cos(angle))
        if two_k < period:
            cols.append(np.sin(angle))
    return np.column_stack(cols)


def regression_rss(x: np.ndarray, candidates: np.ndarray, harmonics: int) -> np.ndarray:
    n = x.shape[0]
    t = np.arange(n, dtype=float)
    rss = np.empty(candidates.shape[0], dtype=float)
    for i, period in enumerate(candidates):
        design = harmonic_design_matrix(t, float(period), harmonics)
        coeffs, _, _, _ = np.linalg.lstsq(design, x, rcond=None)
        fitted = design @ coeffs
        rss[i] = float(np.sum((x - fitted) ** 2))
    return rss


def regression_case(x: np.ndarray, harmonics: int) -> dict:
    n = x.shape[0]
    p_min, p_max = 2.0, n / 2.0
    candidates = np.arange(np.ceil(p_min), np.floor(p_max) + 1, dtype=float)
    rss = regression_rss(x, candidates, harmonics)

    mean_y = float(np.mean(x))
    sst = float(np.sum((x - mean_y) ** 2))
    min_rss = float(np.min(rss))
    threshold = min_rss + 1e-10 * sst
    eligible = [i for i, r in enumerate(rss) if r <= threshold]
    best_idx = min(eligible, key=lambda i: candidates[i])
    period = float(candidates[best_idx])

    return {
        "n": n,
        "x": x,
        "harmonics": harmonics,
        "candidates": candidates,
        "expected": {
            "rss": rss,
            "period": period,
        },
    }


def make_regression_cases() -> dict[str, dict]:
    cases: dict[str, dict] = {}

    t144 = np.arange(144, dtype=float)
    shape12 = (t144 % 12) / 12 + 0.5 * np.sin(2 * np.pi * t144 / 12) + 0.2 * np.random.default_rng(
        15705
    ).normal(0, 1, 144)
    cases["shape12_n144"] = regression_case(np.round(shape12, 4) + 0.0, 3)

    t120 = np.arange(120, dtype=float)
    pure_sine = np.sin(2 * np.pi * t120 / 12)
    cases["pure_sine12_n120"] = regression_case(pure_sine, 3)

    return cases


# ---------------------------------------------------------------------------
# Group 4: Lomb-Scargle (D-11, G21)
# ---------------------------------------------------------------------------


def lombscargle_case(
    x: np.ndarray, min_period: float, max_period: float, n_frequencies: int
) -> dict:
    n = x.shape[0]
    t = np.arange(n, dtype=float)

    min_freq = 1.0 / max_period
    max_freq = 1.0 / min_period
    freqs = np.linspace(min_freq, max_freq, n_frequencies)

    mean = float(np.mean(x))
    y = x - mean
    power = scipy_lombscargle(t, y, 2 * np.pi * freqs, normalize=True) * n / 2.0

    best_idx = int(np.argmax(power))
    best_freq = float(freqs[best_idx])
    period = 1.0 / best_freq if best_freq > 0.0 else None

    return {
        "n": n,
        "x": x,
        "min_period": min_period,
        "max_period": max_period,
        "n_frequencies": n_frequencies,
        "expected": {
            "freqs": freqs,
            "power": power,
            "period": period,
        },
    }


def make_lombscargle_cases() -> dict[str, dict]:
    cases: dict[str, dict] = {}

    t60 = np.arange(60, dtype=float)
    sine12 = np.sin(2 * np.pi * t60 / 12) + 0.3 * np.random.default_rng(1).normal(0, 1, 60)
    sine12 = np.round(sine12, 4) + 0.0
    t_span = t60[-1] - t60[0]
    cases["sine12_n60"] = lombscargle_case(sine12, 2.0, t_span / 2.0, 1000)

    return cases


# ---------------------------------------------------------------------------
# Group 5: seasonal-strength detrending (D-12, G19)
# ---------------------------------------------------------------------------


def strength_case(x: np.ndarray) -> dict:
    detrended = scipy_detrend(x, type="linear")
    return {
        "n": x.shape[0],
        "x": x,
        "expected": {
            "detrended": detrended,
        },
    }


def make_strength_cases() -> dict[str, dict]:
    cases: dict[str, dict] = {}

    t120 = np.arange(120, dtype=float)
    cases["trend_sine_n120"] = strength_case(0.5 * t120 + 2.0 * np.sin(2 * np.pi * t120 / 12))

    noise = np.random.default_rng(15706).normal(0, 1, 120)
    cases["noise_n120"] = strength_case(np.round(noise, 4) + 0.0)

    return cases


def main() -> None:
    write_fixture(
        family="period_detection",
        filename="cases.json",
        generator=generator_path(__file__),
        versions=tool_versions("numpy", "scipy", "statsmodels"),
        payload={
            "tolerance": TOLERANCE,
            "periodogram_cases": make_periodogram_cases(),
            "acf_cases": make_acf_cases(),
            "regression_cases": make_regression_cases(),
            "lombscargle_cases": make_lombscargle_cases(),
            "strength_cases": make_strength_cases(),
        },
    )


if __name__ == "__main__":
    main()
