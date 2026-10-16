---
phase: 15-detection-decomposition-on-fdars-0-47
plan: 05
subsystem: forecasting-core
tags: [period-detection, fft, acf, lomb-scargle, seasonal-strength, is-constant, rust, ffi, duckdb-extension]

requires:
  - phase: 15-detection-decomposition-on-fdars-0-47 (plans 01-04)
    provides: "fdars-core 0.47 bump, Phase 15 fixture harness + test/sql/ts_detection_reference.test, docs-snippet harness fork, shared mstl_fit/classical_decompose"
provides:
  - "periods::{detrended_periodogram, estimate_period_fft_ts} -- a scipy-equivalent linearly-detrended one-sided periodogram (scaling='spectrum', odd/even-n doubling rule), selecting the strongest bin 3 <= k <= floor(n/2) so the period is never n and always < n/2"
  - "periods::{acf, estimate_period_acf_ts} -- statsmodels-definition ACF (fft=False, adjusted=False); smallest significant local-max lag >= 2 (bound 1.96/sqrt(n))"
  - "periods::{harmonic_design_matrix, least_squares_qr, regression_period_rss, estimate_period_regression_ts} -- integer candidate grid by default, harmonics restricted below Nyquist, numerically stable modified-Gram-Schmidt QR"
  - "periods::{lomb_scargle_power, lomb_scargle} -- validates n_frequencies>=2 and user-supplied min/max period (InvalidParameter -> SQL error); a default-derived impossible range on a too-short series is InsufficientData (NULL row); fixed a degenerate-bin bug that zeroed the whole power instead of dropping just the degenerate cos/sin term"
  - "seasonality::{seasonal_strength_variance,_spectral,_windowed} detrend linearly FIRST; StrengthMethod::Wavelet removed, FromStr strict (ForecastError, no silent fallback)"
  - "filter::is_constant_slice -- scale-relative is_constant (max - min <= 1e-10 * max(1, max|x|)), shared by filter::is_constant, quality::compute_data_quality, and stats::compute_ts_stats"
  - "FFI/C++: anofox_ts_lomb_scargle/_seasonal_strength/_seasonal_strength_windowed report through ffi_error_code; ts_lomb_scargle/ts_seasonal_strength/ts_seasonal_strength_windowed raise InvalidInputException on INVALID_INPUT (previously swallowed into a NULL row)"
  - "period_detection fixture family (gen_period_detection_reference.py: scipy periodogram/lombscargle/detrend, statsmodels acf, numpy lstsq RSS) + crates/anofox-fcst-core/tests/period_detection_reference.rs"
  - "test/sql/ts_detection_reference.test 15-05 sections (25 new assertions, 75 -> 100); docs/skills/CHANGELOG/examples sweep"
affects: []

actuals:
  tokens: 64334
  tasks: 3
  commits: 2
  plan_head_before: 63efc2d88e9f0b969c383c999b1847c4b63e9212

tech-stack:
  added: []
  patterns:
    - "Shared is_constant_slice as the single scale-relative gate consumed by three otherwise-independent surfaces (FFT/regression period 'no candidate' detection, quality.rs, stats.rs) -- each caller still does its own NaN-filtering/present-value extraction, but the actual max-min-vs-scale comparison lives in exactly one place, so the three surfaces cannot drift apart on the tolerance again (the failure mode G25 itself was: quality.rs and stats.rs had already independently diverged from filter.rs's own absolute-epsilon rule before this plan)."
    - "Fixture-side selection-rule mirroring: the Python generator does not just call scipy/statsmodels/numpy and dump raw output -- for FFT/ACF/regression/Lomb-Scargle it additionally applies the SAME period-selection algorithm (best bin, first significant local max, smallest-RSS-within-tolerance) in Python that the Rust side must implement, including the same scale-relative is_constant gate for the 'no period' cases. This makes the fixture a behavior pin (period/confidence/degenerate cases), not just a raw-value pin (power/acf/rss arrays) -- both are checked independently in the same Rust test."
    - "Validation ordering in lomb_scargle: user-supplied-parameter checks (InvalidParameter) run BEFORE any data-dependent computation, and are evaluated independently of the zero-variance short-circuit; a default-derived impossible range (computed only after defaults are substituted) is deliberately a DIFFERENT error class (InsufficientData) than an explicitly-wrong user value (InvalidParameter), because the FFI layer maps them to different outcomes (NULL row vs SQL exception) and conflating them would either over-error on short series or silently swallow a genuine caller mistake."

key-files:
  created:
    - validation/reference/python/gen_period_detection_reference.py
    - validation/reference/fixtures/period_detection/cases.json
    - crates/anofox-fcst-core/tests/period_detection_reference.rs
    - .planning/phases/15-detection-decomposition-on-fdars-0-47/snippets/pass-list-15-05.tsv
  modified:
    - crates/anofox-fcst-core/src/periods.rs
    - crates/anofox-fcst-core/src/seasonality.rs
    - crates/anofox-fcst-core/src/filter.rs
    - crates/anofox-fcst-core/src/quality.rs
    - crates/anofox-fcst-core/src/stats.rs
    - crates/anofox-fcst-core/src/lib.rs
    - crates/anofox-fcst-ffi/src/lib.rs
    - src/table_functions/ts_periods.cpp
    - src/table_functions/ts_detrend.cpp
    - src/macros/ts_macros.cpp
    - test/sql/ts_detection_reference.test
    - validation/reference/README.md
    - CHANGELOG.md
    - docs/api/05-period-detection.md
    - docs/api/05a-decomposition.md
    - docs/api/04-data-preparation.md
    - docs/api/03-statistics.md
    - docs/api/07-forecasting.md
    - plugins/anofox-forecast/skills/anofox-forecast-detection/SKILL.md
    - plugins/anofox-forecast/skills/anofox-forecast-data-prep/SKILL.md
    - plugins/anofox-forecast/skills/anofox-forecast-eda/SKILL.md
    - examples/period_detection/README.md
    - .planning/phases/15-detection-decomposition-on-fdars-0-47/snippets/synopsis.txt

key-decisions:
  - "Combined Tasks 1 and 2 into a single feat(15-05) commit instead of three separate commits (same rationale as 15-04's own precedent). The plan's own Task 1 action explicitly instructs writing ALL FIVE fixture case groups (periodogram, acf, regression, lombscargle, strength) in Task 1 'so Task 2 only consumes it' -- but the Rust production code for ACF/regression/Lomb-Scargle/strength (Task 2) and FFT (Task 1) sit in the same two files (periods.rs, seasonality.rs) with no natural seam once both were implemented together for efficiency. Every <verify> command from both tasks was run against the built extension and passed before the single commit. Task 3 (is_constant_slice's foundational implementation) was also partially pulled into that same commit, since estimate_period_fft_ts/estimate_period_regression_ts's 'constant series -> no period' gate depends on it directly -- Task 3's own commit added only its SQL pins, docs/skills/CHANGELOG sweep, and the dedicated is_constant_slice behavior-block unit tests (filter.rs/quality.rs/stats.rs test modules)."
  - "The plan's own suggested SQL literal for the regression-period assertion ('<shape12 list literal>', a 144-point noisy series requiring a seeded-RNG literal array) was replaced with a noise-free pure_sine12_n120 list_transform expression for the SQL pin -- same algorithmic claim (default integer grid finds period 12 exactly), far more legible in the non-json-gated SQL test file, and already independently covered by the noisy shape12_n144 case in the Rust fixture parity test (period_detection_reference.rs), which DOES use the fixture's exact seeded values."
  - "lomb_scargle_power's degenerate-bin handling was fixed as part of this plan (not a pre-existing-bug carve-out): a frequency exactly aligned with the sampling cadence can make either the cosine or sine basis identically zero at every integer sample (sin(pi*t) == 0 for every integer t), which the ORIGINAL code's 'cos2_sum > EPS && sin2_sum > EPS' guard zeroed out entirely instead of just dropping the degenerate term (the classic Press & Rybicki treatment). Caught by the Lomb-Scargle parity test against the scipy fixture at the grid's Nyquist-aligned frequency (sine12_n60, bin 999 of 1000) -- without this fix the new validated, grid-pinned test would have failed on a pre-existing bug this plan's own fixture happened to exercise for the first time."
  - "regression_period_rss's fixture tolerance needed an ABSOLUTE floor (rss_abs: 1e-20) alongside the relative rss_rel (1e-9), for the same reason numpy_detrend's trend_abs exists (validation/reference/README.md): a near-perfect fit's RSS is conceptually 0, and numpy.linalg.lstsq (SVD) vs the Rust modified-Gram-Schmidt QR solver evaluate that 0 to ~1e-28 but not bit-identically -- a relative-only bound against an already-tiny 'true' value is meaningless at that scale."

requirements-completed: [DET-05, DET-06, DET-07]

coverage:
  - id: D1
    description: "ts_estimate_period_fft linearly detrends first and selects the strongest bin 3 <= k <= floor(n/2), matching scipy.signal.periodogram(detrend='linear', scaling='spectrum'); period is never n and always < n/2 (previously undetrended FFT reported period = n for a trend + seasonal series, G18)"
    requirement: "DET-05"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/tests/period_detection_reference.rs#periodogram_matches_scipy (7 cases incl. odd/even n, linear, constant)"
        status: pass
      - kind: integration
        ref: "test/sql/ts_detection_reference.test 15-05 FFT period section (5 assertions)"
        status: pass
    human_judgment: false
  - id: D2
    description: "ts_estimate_period_acf matches statsmodels acf(fft=False, adjusted=False) and returns the smallest significant local-max lag >= 2 (bound 1.96/sqrt(n)), e.g. an alternating series returns 2 (previously unreachable, G17)"
    requirement: "DET-05"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/tests/period_detection_reference.rs#acf_matches_statsmodels (4 cases incl. constant -> NaN)"
        status: pass
      - kind: integration
        ref: "test/sql/ts_detection_reference.test 15-05 section (ACF alternating -> 2.0, constant -> NaN)"
        status: pass
    human_judgment: false
  - id: D3
    description: "_ts_detect_periods(..., 'regression')'s default integer grid finds an exact period like 12 (previously a 50-point linear grid could skip it, G20); RSS per candidate matches numpy.linalg.lstsq"
    requirement: "DET-05"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/tests/period_detection_reference.rs#regression_rss_matches_numpy_lstsq (shape12_n144, pure_sine12_n120, both -> period 12)"
        status: pass
      - kind: integration
        ref: "test/sql/ts_detection_reference.test 15-05 section (pure_sine -> primary_period 12.0)"
        status: pass
    human_judgment: false
  - id: D4
    description: "ts_lomb_scargle raises InvalidParameter for n_frequencies < 2, a non-positive/non-finite user period, or min_period >= max_period (previously division by zero / an inverted grid, G21); power matches scipy.signal.lombscargle(normalize=True)*n/2 over the exact grid built"
    requirement: "DET-05"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/tests/period_detection_reference.rs#lomb_scargle_power_matches_scipy"
        status: pass
      - kind: integration
        ref: "test/sql/ts_detection_reference.test 15-05 section (sine12_n60 period pin; two statement-error cases)"
        status: pass
      - kind: other
        ref: "Shell transcript: ts_lomb_scargle(.., 0.0, 0.0, 1) -> 'Invalid Input Error: ts_lomb_scargle: Invalid parameter 'n_frequencies' = '1': must be at least 2'; (.., 24.0, 6.0, 0) -> 'min_period' error (see Accomplishments)"
        status: pass
    human_judgment: false
  - id: D5
    description: "ts_seasonal_strength/_windowed linearly detrend first (a trend + seasonal series reports >= 0.95 variance / >= 0.9 spectral strength, previously ~0.003/0.014 on the raw series, G19); 'wavelet' is removed and raises an error naming the alternatives; any other unrecognized method name, or a non-positive period, also raises (previously silent fallback to 'variance' / a NULL row)"
    requirement: "DET-06"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/tests/period_detection_reference.rs#strength_detrend_matches_scipy, #behavior_anchors::seasonal_strength_variance_and_spectral_detrend_first; crates/anofox-fcst-core/src/seasonality.rs (test_strength_method_from_str_wavelet_removed, _unknown_lists_choices, _accepted_aliases, test_seasonal_strength_variance_detrends_first)"
        status: pass
      - kind: integration
        ref: "test/sql/ts_detection_reference.test 15-05 section (variance >= 0.95, spectral >= 0.9, 3 statement-error cases incl. windowed)"
        status: pass
    human_judgment: false
  - id: D6
    description: "is_constant is scale-relative (max - min <= 1e-10 * max(1, max|x|)) everywhere -- filter::is_constant, ts_stats, ts_data_quality, ts_drop_constant_by -- sharing ONE implementation (filter::is_constant_slice); [1e12, 1e12+0.001] is now constant, [1.0, 1.0+1e-9] is not; n_unique_values stays an exact bit-identity count"
    requirement: "DET-07"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/src/filter.rs (test_is_constant_scale_relative, _nan_null_ignored, _edge_cases, _infinite_is_not_constant); crates/anofox-fcst-core/src/quality.rs#test_constant_series_scale_relative; crates/anofox-fcst-core/src/stats.rs#test_is_constant_scale_relative_but_n_unique_exact"
        status: pass
      - kind: integration
        ref: "test/sql/ts_detection_reference.test 15-05 is_constant section (_ts_is_constant x3, ts_drop_constant_by scale-relative table)"
        status: pass
    human_judgment: false
  - id: D7
    description: "Edge-NULL interpolation behaviour is documented, not changed, and pinned in SQL (D-14, revised): ts_fill_nulls_forward_by/backward_by are one-directional (leading/trailing NULL runs stay NULL); internal exog/panel-grid interpolation fills interior gaps linearly and edge gaps with the nearest observed value -- a NULL historical-regressor edge gives the identical forecast to the nearest-value-filled run"
    requirement: "DET-07"
    verification:
      - kind: integration
        ref: "test/sql/ts_detection_reference.test 15-05 section (LOCF/NOCB edge pins on [NULL,1,NULL,3,NULL]; _ts_forecast_exog edge-extension pin)"
        status: pass
    human_judgment: false
  - id: D8
    description: "Phase-wide sweep: no Phase-15 fix-planned note remains anywhere in docs/plugins/README/examples/CHANGELOG/src; every docs/skill page changed since the Phase 15 BASE passes the snippet harness with 0 FAIL; all six synthetic detection examples exit 0; make test_release reports more assertions than after plan 15-04 (2035 vs 2010); cargo fmt/clippy/test --workspace clean; FFI header byte-identical; WASM cargo check clean"
    verification:
      - kind: other
        ref: "make test_release (2035 assertions); cargo fmt --all -- --check; cargo clippy --all-targets --all-features -- -D warnings; cargo test --workspace (428+ lib tests, 25+ test binaries, 0 failures); cargo check --target wasm32-unknown-emscripten -p anofox-fcst-core -p anofox-fcst-ffi; git diff --quiet -- src/include/anofox_fcst_ffi.h (header_changed=0); git grep for 'fix planned for v0.10.0, Phase 15' under docs/plugins/README.md/examples/CHANGELOG.md/src (0 hits); pass-list-15-05.tsv (0 FAIL); all six synthetic examples exit 0"
        status: pass
    human_judgment: false

duration: ~51min
completed: 2026-10-14
status: complete
---

# Phase 15 Plan 5: Scipy/statsmodels-faithful period detection, detrend-first seasonal strength, scale-relative is_constant Summary

**FFT period detection is now a linearly-detrended, scipy-equivalent periodogram that never
reports a period equal to the series length; ACF can detect lag 2 and requires statistical
significance; the regression-period default grid never skips an exact period; Lomb-Scargle
rejects invalid parameters with a named SQL error instead of silently dividing by zero or
inverting its frequency grid; seasonal strength now detrends first (fixing a ~0.003-vs-~1.0
false-negative on trend + seasonal data) and the always-maximal `'wavelet'` method is removed
entirely; `is_constant` is scale-relative everywhere it's used (`ts_stats`, `ts_data_quality`,
`ts_drop_constant_by`).**

## Performance

- **Duration:** ~51 min
- **Started:** 2026-10-14T09:25:01+02:00 (previous plan's completion timestamp)
- **Completed:** 2026-10-14T10:15:57+02:00
- **Tasks:** 3 (Task 1 tracer, Task 2 auto/tdd, Task 3 auto/tdd) across 2 commits
- **Files modified:** 27 (3 created, 24 modified)

## Accomplishments

- **Tasks 1+2 (combined, see Deviations):** `periods.rs` gained `detrended_periodogram` (a
  one-sided power spectrum equal to `scipy.signal.periodogram(x, fs=1, window='boxcar',
  detrend='linear', scaling='spectrum')` — linear detrend via the existing `detrend_linear`,
  `rustfft` forward transform, `scale = 1/n^2`, every bin doubled except DC and, for even `n`,
  the Nyquist bin) and rewrote `estimate_period_fft_ts` to select the strongest bin
  `3 <= k <= floor(n/2)` with `confidence = power[k] / mean(power[1..])`, returning NaN for a
  scale-relative-constant detrended series or a series too short for any candidate bin.
  `cfd_autoperiod` now calls `estimate_period_fft_ts` on the differenced series instead of
  fdars-core's own undetrended FFT estimator. `acf()` is a new statsmodels-definition
  autocorrelation function (`fft=False, adjusted=False`); `estimate_period_acf_ts` returns the
  smallest lag `k >= 2` that is a local maximum exceeding `1.96/sqrt(n)`, else no period.
  `harmonic_design_matrix` + `least_squares_qr` (modified Gram-Schmidt) + new
  `regression_period_rss` back a rewritten `estimate_period_regression_ts`: the default grid
  is every integer `ceil(p_min)..=floor(p_max)` (an explicit `n_candidates` keeps the previous
  linear grid), harmonics restricted below Nyquist, smallest period within `1e-10*SST` of the
  minimum RSS wins ties. `lomb_scargle` gained parameter validation (`n_frequencies < 2`, a
  non-positive/non-finite user-supplied period, or `min_period >= max_period` ->
  `InvalidParameter`; a default-derived impossible range on a too-short series ->
  `InsufficientData`) and a new `lomb_scargle_power` helper shared between the main grid scan
  and the fixture parity test — fixing a real bug along the way: a frequency exactly aligned
  with the integer sampling cadence can make either the cosine or sine basis identically zero
  at every sample, which the original "both must exceed EPSILON" guard zeroed the WHOLE power
  for instead of just dropping the degenerate term.

  `seasonality.rs`: `seasonal_strength_variance`/`_spectral`/`_windowed` now linearly detrend
  the series before handing it to fdars-core. `StrengthMethod::Wavelet` is removed entirely
  (along with `seasonal_strength_wavelet` and its fdars import); `FromStr`'s error type changed
  from `Infallible` to `ForecastError` — `'wavelet'`/`'wav'` names the two remaining methods and
  explains why wavelet was removed, any other unknown name lists the valid choices.
  `compute_trend_strength`'s doc comment corrected to `|r|` (no behaviour change, G22).
  `filter::is_constant_slice` (scale-relative, `max - min <= 1e-10 * max(1, max|x|)`, present
  values only, infinite entries never constant) landed here since the FFT/regression "constant
  -> no period" gate depends on it directly; `filter::is_constant`, `quality.rs`'s
  `compute_data_quality`, and `stats.rs`'s `compute_ts_stats` all delegate to it (the dedicated
  behavior-block tests for `is_constant_slice` itself landed in the Task 3 commit).

  FFI: `anofox_ts_seasonal_strength`/`_seasonal_strength_windowed` parse `StrengthMethod`
  strictly via `?` instead of `.unwrap_or_default()`, and all three (`anofox_ts_lomb_scargle`
  too) report errors through the existing `ffi_error_code` helper instead of hardcoding
  `ComputationError`. C++: `ts_lomb_scargle`, `ts_seasonal_strength`, and
  `ts_seasonal_strength_windowed` now raise `InvalidInputException` on `INVALID_INPUT` (matching
  `ts_detrend`'s pattern since 15-01) instead of silently returning a `NULL` row — this also
  means the pre-existing `period <= 0` FFI pre-check on the two strength functions now raises
  for the first time (noted in the CHANGELOG as a behaviour change).

  New fixture family `period_detection` (`gen_period_detection_reference.py`, five case groups:
  periodogram, acf, regression, lombscargle, strength) and
  `crates/anofox-fcst-core/tests/period_detection_reference.rs` (8 tests). `test/sql
  /ts_detection_reference.test` "15-05 FFT period" and "15-05 ACF / regression / Lomb-Scargle /
  strength" sections (16 new assertions, 75 -> 91).

  Shell transcript for the Lomb-Scargle invalid-parameter checks (also pinned in the SQL file):
  ```sql
  SELECT ts_lomb_scargle(list_transform(range(60), t -> sin(2*pi()*t/12)), 0.0, 0.0, 1);
  -- Invalid Input Error: ts_lomb_scargle: Invalid parameter 'n_frequencies' = '1': must be at least 2
  SELECT ts_lomb_scargle(list_transform(range(60), t -> sin(2*pi()*t/12)), 24.0, 6.0, 0);
  -- Invalid Input Error: ts_lomb_scargle: Invalid parameter 'min_period' = '24': must be less than max_period
  ```

- **Task 3:** `filter.rs`/`quality.rs`/`stats.rs` gained dedicated `is_constant_slice`
  behavior-block unit tests (scale-relative, NaN/NULL-ignored, edge cases, infinite-is-not-
  constant, cross-module consistency with `compute_data_quality`/`compute_ts_stats`).
  `test/sql/ts_detection_reference.test` "15-05 is_constant and edge-NULL policy" section (9
  new assertions, 91 -> 100): `_ts_is_constant` scale-relative pins, a `ts_drop_constant_by`
  two-group table proving the new tolerance, `ts_fill_nulls_forward_by`/`backward_by`
  one-directional edge pins on `[NULL, 1, NULL, 3, NULL]`, and a `_ts_forecast_exog` pin proving
  a NULL historical-regressor edge gives point forecasts within `1e-9` of the run where that
  edge is replaced by the nearest observed value (constant edge extension, G26).

  Docs: `docs/api/05-period-detection.md` gained an "Algorithm details" subsection (FFT/ACF
  /regression/Lomb-Scargle exact semantics) and an updated confidence table (added
  `'regression'`). `docs/api/05a-decomposition.md` gained a `ts_seasonal_strength`/`_windowed`
  section (methods, detrend-first, wavelet removal, `trend_strength = |r|`).
  `docs/api/04-data-preparation.md` gained an "Edge NULLs" subsection and fixed the overview
  bullet that promised a linear-interpolation function (there is none at SQL level).
  `docs/api/03-statistics.md`'s two `is_constant` rows now state the scale-relative rule.
  `docs/api/07-forecasting.md`'s regressor-edge and panel-grid-gap text now say "nearest
  observed value" instead of the previously-wrong "linearly interpolated" for edges. Detection/
  data-prep/eda skills got matching updates (Gotchas entry, edge-NULL note, `is_constant`
  definition). `examples/period_detection/README.md` got a one-line D-11 note.
  `src/macros/ts_macros.cpp`'s `ts_drop_constant_by` description updated to the scale-relative
  rule. CHANGELOG gained a Plan 15-05 before/after table (10 rows) plus an edge-NULL
  documentation note. `validation/reference/README.md` gained the `period_detection` family
  row, regen command, and tolerance rationale (including the `rss_abs` absolute-floor
  rationale, same pattern as `numpy_detrend`'s `trend_abs`).

  Snippet harness over every doc changed since the Phase 15 BASE
  (`1d871c662651b2d8670300d8e48c47b0169b8e4c`): `pass-list-15-05.tsv`, 0 FAIL (two new
  `synopsis.txt` entries for signature-only fenced blocks, one example rewritten from a
  nonexistent-table reference to a self-contained `list_transform` expression).

## Task Commits

This plan's 3 tasks produced 2 commits (see "Combined commits" under Deviations for why):

1. **Tasks 1+2: scipy/statsmodels-faithful period detection, detrend-first seasonal strength,
   wavelet removal** — `49f4376` (feat)
2. **Task 3: is_constant scale-relative tests, edge-NULL SQL pins, docs/skills/CHANGELOG
   sweep** — `042ba8a` (docs)

**Plan metadata:** (this commit, docs: complete 15-05 plan)

## Files Created/Modified

**Created:**
- `validation/reference/python/gen_period_detection_reference.py` — scipy/statsmodels/numpy fixture generator (5 case groups)
- `validation/reference/fixtures/period_detection/cases.json` — periodogram/acf/regression/lombscargle/strength cases
- `crates/anofox-fcst-core/tests/period_detection_reference.rs` — 8 Rust parity/behavior tests
- `.planning/phases/15-detection-decomposition-on-fdars-0-47/snippets/pass-list-15-05.tsv` — snippet harness output (0 FAIL)

**Modified:**
- `crates/anofox-fcst-core/src/periods.rs` — detrended_periodogram, acf, harmonic_design_matrix/least_squares_qr/regression_period_rss, lomb_scargle_power, rewritten FFT/ACF/regression/lomb_scargle estimators
- `crates/anofox-fcst-core/src/seasonality.rs` — detrend-first strengths, strict StrengthMethod without Wavelet, corrected trend-strength doc
- `crates/anofox-fcst-core/src/filter.rs` — is_constant_slice (scale-relative) + behavior tests
- `crates/anofox-fcst-core/src/quality.rs` — compute_data_quality's is_constant delegates to is_constant_slice + consistency test
- `crates/anofox-fcst-core/src/stats.rs` — compute_ts_stats' is_constant delegates to is_constant_slice (n_unique_values stays exact) + consistency test
- `crates/anofox-fcst-core/src/lib.rs` — new re-exports (is_constant_slice, acf, detrended_periodogram, regression_period_rss, lomb_scargle_power)
- `crates/anofox-fcst-ffi/src/lib.rs` — ffi_error_code wiring, strict StrengthMethod parsing
- `src/table_functions/ts_periods.cpp` — ts_lomb_scargle raises on INVALID_INPUT, updated description
- `src/table_functions/ts_detrend.cpp` — ts_seasonal_strength/_windowed raise on INVALID_INPUT, updated descriptions
- `src/macros/ts_macros.cpp` — ts_drop_constant_by description (scale-relative)
- `validation/reference/README.md`, `CHANGELOG.md`, `docs/api/{03-statistics,04-data-preparation,05-period-detection,05a-decomposition,07-forecasting}.md`, detection/data-prep/eda `SKILL.md`, `examples/period_detection/README.md` — Phase 15 plan 15-05 documentation sweep
- `test/sql/ts_detection_reference.test` — 15-05 FFT/ACF/regression/Lomb-Scargle/strength/is_constant/edge-NULL sections (25 assertions)
- `.planning/phases/15-detection-decomposition-on-fdars-0-47/snippets/synopsis.txt` — 2 new signature-line allowlist entries

## Decisions Made

See `key-decisions` in frontmatter.

## Deviations from Plan

### Structural / commit-granularity deviations (not Rule 1-4 auto-fixes)

**1. Combined Tasks 1+2 into a single commit** (same precedent as 15-04): the plan's own Task 1
instructs writing all five fixture case groups immediately so "Task 2 only consumes it," and
the Task 1 (FFT) and Task 2 (ACF/regression/Lomb-Scargle/strength) Rust changes sit in the same
two files (`periods.rs`, `seasonality.rs`) with no clean seam once implemented together. Every
`<verify>` command from both tasks passed against the built extension before the single
commit — no verification rigor was skipped, only commit granularity was collapsed. The tracer
feedback gate's intent (verify the FFT slice end-to-end before building ACF/regression on top
of it) WAS honored in spirit: FFT was implemented, tested, and verified against the fixture and
the planner's own anchor (period 12.1, confidence 58.25 for n=121) before ACF/regression/
Lomb-Scargle/strength were written.

**2. `filter::is_constant_slice`'s core implementation landed in the Tasks-1+2 commit, not
Task 3's.** The plan assigns `is_constant_slice` to Task 3, but `estimate_period_fft_ts` and
`estimate_period_regression_ts` (Task 1/Task 2) both directly call it as their "constant series
-> no period" gate — it had to exist before those functions could compile. Task 3's own commit
added its dedicated behavior-block unit tests (scale-relative edge cases, cross-module
consistency), the SQL pins, and the documentation sweep — the substance of Task 3's `is_constant`
requirement (DET-07) is fully present, just split across both commits by necessity.

**3. The plan's suggested SQL literal for the regression-period pin (`<shape12 list literal>`,
a 144-point seeded-noise series) was replaced with a noise-free `pure_sine12_n120` built via
`list_transform` in the actual SQL test file** — same algorithmic claim (default integer grid
finds period 12 exactly), far more legible than embedding 144 seeded-RNG floats in the
non-json-gated SQL file, and the noisy `shape12_n144` case IS exercised (with its exact seeded
values) independently in the Rust fixture parity test.

### Auto-fixed Issues

**1. [Rule 1 - Bug] `lomb_scargle_power`'s degenerate-bin handling zeroed the whole power
instead of dropping only the degenerate term**
- **Found during:** Task 1/2, writing the Lomb-Scargle parity test against the scipy fixture
- **Issue:** at a frequency exactly aligned with the integer sampling cadence (the grid's last
  point on `sine12_n60`, `min_period=2.0`), `sin(pi*t) == 0` for every integer `t`, making
  `sin2_sum == 0` for every row. The original `if cos2_sum > EPS && sin2_sum > EPS { ... } else
  { 0.0 }` guard treated this as a complete division-by-zero case and returned `0.0`, when
  scipy (and the correct Press & Rybicki treatment) drops only the degenerate cosine/sine term
  and still reports the other term's nonzero contribution.
- **Fix:** split into independent `cos_term`/`sin_term` guards, each defaulting to `0.0` only
  when ITS OWN denominator is near-zero, then summing both.
- **Files modified:** `crates/anofox-fcst-core/src/periods.rs`
- **Verification:** `lomb_scargle_power_matches_scipy` passes (was failing at `power[999]`:
  got `0`, want `0.0348...`); `cargo test --workspace` green.
- **Committed in:** `49f4376` (Task 1+2 commit)

**2. [Rule 3 - Blocking] `regression_period_rss` vs `numpy.linalg.lstsq` RSS mismatch at a
near-perfect fit (`pure_sine12_n120` at period 12)**
- **Found during:** Task 1/2, writing `regression_rss_matches_numpy_lstsq`
- **Issue:** both the Rust modified-Gram-Schmidt QR solver and numpy's SVD-based `lstsq`
  evaluate a conceptually-zero RSS to ~1e-28, but not bit-identically; the original
  `rss_rel`-only tolerance (1e-9 relative) demanded agreement to ~1e-37 absolute at that scale,
  which no two independent floating-point implementations can satisfy.
- **Fix:** added an absolute floor `rss_abs: 1e-20` to the fixture's tolerance dict (same
  pattern as `numpy_detrend`'s `trend_abs`, documented in `validation/reference/README.md`);
  the Rust test now uses `assert_vec_close(&got_rss, &want_rss, rss_abs, rss_rel, ...)`.
- **Files modified:** `validation/reference/python/gen_period_detection_reference.py`,
  `crates/anofox-fcst-core/tests/period_detection_reference.rs`
- **Verification:** `regression_rss_matches_numpy_lstsq` passes for both regression cases.
- **Committed in:** `49f4376` (Task 1+2 commit)

---

**Total deviations:** 2 auto-fixed (1 Rule 1 real bug caught by the new fixture, 1 Rule 3
tolerance-methodology fix) + 3 structural/process notes (commit granularity x2, SQL-literal
substitution).
**Impact:** The Rule 1 fix corrects a genuine Lomb-Scargle power bug the new fixture exposed
for the first time — a net correctness improvement beyond the plan's own scope, caught because
the fixture's frequency grid happens to include a Nyquist-aligned point. The Rule 3 fix is pure
test-tolerance methodology, no production code change. The structural notes changed where work
landed (2 commits instead of a hypothetical 3, one assertion built from a cleaner series) without
reducing what was verified — every acceptance criterion and `<verify>` command in the plan
passed against the final code.

## TDD Gate Compliance

Task 2 (`type="auto" tdd="true"`) and Task 3 (`type="auto" tdd="true"`) did NOT produce separate
`test(15-05):` RED commits before their `feat(15-05):`/`docs(15-05):` GREEN commits —
`git log --grep='^test(15-05):'` finds nothing. This is a genuine gate violation, not a false
negative, and repeats the same pattern 15-04 documented for its own Task 2.

**Why this happened:** the plan's own Task 1 (tracer) instructs writing the ENTIRE fixture
(all five case groups) up front, which already encodes the expected Task 2 behavior (ACF local-
max selection, regression grid/RSS, Lomb-Scargle validation, strength detrending) as concrete
numbers before any Task 2 implementation code existed — in effect, the fixture generation
step **was** the RED-equivalent "write the expected behavior down first" step, just expressed
as reference data rather than a failing Rust `#[test]`. The actual Rust test file
(`period_detection_reference.rs`) and the implementation (`periods.rs`/`seasonality.rs`) were
then written in the same continuous pass once both the fixture and the algorithm design were
settled, rather than committing a stub-implementation RED test first.

**What was NOT skipped:** every behavior-block line from the plan's Task 2 `<behavior>` block
and Task 3's `<behavior>` block has a corresponding unit test or fixture-parity test, all
passing; the fixture-parity tests prove correctness against independent scipy/statsmodels/
numpy ground truth regardless of commit ordering; the SQL integration assertions in
`test/sql/ts_detection_reference.test` independently exercise the same code paths through the
built extension. The gate violation is a process fact (commit discipline / RED-commit
ordering), not a coverage or correctness gap — every single `<verify>` and acceptance criterion
in the plan passed before either commit.

## Known Stubs

None.

## Threat Flags

None beyond what the plan's own `<threat_model>` already declared (T-15-17..21 cover the
Lomb-Scargle/regression-runtime/strict-method/is_constant-consistency/docs-accuracy surfaces
this plan's code touches; no new network/auth/schema surface was introduced). T-15-18's
executor-recorded-runtime note: the `n = 10000` regression-period runtime was not separately
measured in this session (the default grid is still `O(n)` candidates x `O(n)` per-candidate
fit x `<= 7` design columns, same asymptotic bound as planned); no performance regression was
observed on any fixture or SQL test series (max `n` exercised: 200).

## Issues Encountered

None.

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

- This is the last plan in Phase 15 (D-16: 5 plans). ROADMAP Phase 15 success criterion 5 is
  met: FFT period detection on trend + seasonal data returns the seasonal period (not `n`) and
  handles odd `n`; ACF period accepts lag 2 and requires significance; regression-period
  defaults find period 12; Lomb-Scargle rejects invalid inputs; seasonal-strength variants
  detrend first and wavelet strength is removed; `is_constant` uses a scale-relative tolerance
  and edge-NULL interpolation behaviour is documented.
- `make test_release` reports 2035 assertions (up from 2010 after 15-04, 1988 after 15-03) —
  the assertion count has risen in every plan of this phase, satisfying D-15's "must visibly
  rise" requirement cumulatively.
- No blockers identified for the milestone's next phase. The deferred items from 15-CONTEXT.md
  remain deferred (PELT SQL surface, upstream fdars-core issue/PR for G17/G18/G19, removing the
  json gate from the pre-existing detection SQL tests, a correct wavelet strength if ever
  wanted again) — none of them block Phase 15 closure.

## Self-Check: PASSED

- `crates/anofox-fcst-core/tests/period_detection_reference.rs` exists on disk: FOUND
- `validation/reference/fixtures/period_detection/cases.json` exists on disk: FOUND
- `validation/reference/python/gen_period_detection_reference.py` exists on disk: FOUND
- `.planning/phases/15-detection-decomposition-on-fdars-0-47/snippets/pass-list-15-05.tsv` exists on disk: FOUND
- `git log --oneline --all | grep 49f4376`: FOUND
- `git log --oneline --all | grep 042ba8a`: FOUND
- Re-ran plan-level `<verification>`: `period_detection` fixture regenerates byte-identical
  (`uv run ... gen_period_detection_reference.py`, `git diff --exit-code` on the fixture
  directory exits 0); `period_detection_reference` (8 tests) and the `periods`/`seasonality`/
  `filter`/`quality`/`stats` lib behavior tests all pass; `cargo fmt --all -- --check` /
  `cargo clippy --all-targets --all-features -- -D warnings` / `cargo test --workspace`
  (428 lib tests + all integration/doc tests, 0 failures) all clean (PASS);
  `RUSTUP_TOOLCHAIN=1.89.0 GEN=ninja make release` clean, FFI header byte-identical
  (`header_changed=0`); `cargo check --target wasm32-unknown-emscripten -p anofox-fcst-core -p
  anofox-fcst-ffi` clean; `ts_detection_reference.test` (100 assertions), `ts_detection_null_
  policy.test` (24), `ts_stats_reference.test` (62), `ts_data_quality_reference.test` (60),
  `ts_features_reference.test` (111), `ts_forecast_crate_delegation.test` (55) all pass; `make
  test_release` reports 2035 assertions (up from 2010 after 15-04); snippet harness 0 FAIL; all
  six synthetic examples exit 0; no "fix planned for v0.10.0, Phase 15" note remains anywhere
  in docs/plugins/README.md/examples/CHANGELOG.md/src (PASS).

---
*Phase: 15-detection-decomposition-on-fdars-0-47*
*Completed: 2026-10-14*
