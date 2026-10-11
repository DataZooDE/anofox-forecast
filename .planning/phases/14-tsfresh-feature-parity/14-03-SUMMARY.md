---
phase: 14-tsfresh-feature-parity
plan: 03
subsystem: forecasting
tags: [tsfresh, rust, duckdb-extension, statistics, feature-extraction, fft, rustfft, spectral]

requires:
  - phase: 14-tsfresh-feature-parity
    provides: "14-01's mapping.json parity harness (tsfresh_features_reference.rs, 9 fixtures) and its pending:14-03 inventory (31 rows: fft_coefficient_*, spectral_centroid, spectral_variance)"
provides:
  - "fft_coefficient_{0..9}_{real,imag,abs}: numpy-rfft-exact, unnormalised, via rustfft's O(n log n) forward FFT truncated to n/2+1 bins; NaN beyond n/2+1 (D-07)"
  - "spectral_centroid/spectral_variance: tsfresh fft_aggregated moments on the SAME one-sided magnitude spectrum as fft_coefficient_* (previously the two-sided power spectrum)"
  - "rustfft as a direct anofox-fcst-core dependency (Cargo.lock still holds exactly one rustfft package, 6.4.1) -- proven O(n log n) on n = 100003 (prime) and Parseval-identity-correct on n = 131072"
  - "the full FEAT-03 family (31 of 31 previously-pending rows) now at mapping.json status match -- no pending:14-03 rows remain"
affects: [14-04-tsfresh-feature-parity]

actuals:
  tokens: 9770
  tasks: 2
  commits: 3
  plan_head_before: 3223fbb39aa388efbc5f468a81730a0ab8c3540f

tech-stack:
  added: [rustfft (direct dependency, already locked transitively at 6.4.1 via fdars-core)]
  patterns:
    - "fn one_sided_spectrum: single spectrum helper (FftPlanner::<f64>::plan_fft_forward + truncate to n/2+1) feeding BOTH fft_coefficient_* and spectral_centroid/variance -- one FFT computed once per extract_features call, not twice"
    - "fn fft_aggregated_moments: tsfresh get_moment-style centroid/variance with no epsilon guard, so 0.0/0.0 -> NaN propagates exactly like tsfresh on an all-zero series"
    - "Large-n/degenerate-input tests call one_sided_spectrum directly, not through extract_features, so a prime-length n=100003 test doesn't also pay for the other 116 features' cost"

key-files:
  modified:
    - crates/anofox-fcst-core/Cargo.toml
    - Cargo.lock
    - crates/anofox-fcst-core/src/features.rs
    - validation/reference/fixtures/tsfresh_features/mapping.json
    - test/sql/ts_features_reference.test
    - CHANGELOG.md

key-decisions:
  - "Task 2 (tdd=\"true\") produced no feat(14-03) commit: its three behavior tests (prime-n, Parseval, degenerate-input) all passed immediately against Task 1's already-shipped one_sided_spectrum implementation. Investigated per the TDD error-handling rule for an unexpected GREEN -- this is this plan's own D-16 split working as designed (Task 1 = tracer ships the full implementation end-to-end, Task 2 = hardening tests + WASM check + CHANGELOG + DoD), not a process or tooling gap. Committed as test(14-03) alone."
  - "No per-feature tolerance override was needed anywhere in the 31-row FFT/spectral family: rustfft's evaluation order agrees with numpy/tsfresh within the default 1e-9 + 1e-9*|ref| tolerance on every one of the 9 fixture series, including short_n20's largest-magnitude bins (fft_coefficient_0_real = 55.537)."
  - "mapping.json row definitions for the 31 flipped rows were rewritten to drop the stale 'current Rust is an O(n^2) naive DFT...' bug description, replacing it with the new rustfft-based definition -- keeps the mapping file's prose accurate as documentation, not just its status field."

requirements-completed: [FEAT-03]

coverage:
  - id: D1
    description: "fft_coefficient_{0..9}_{real,imag,abs} match numpy rfft exactly (unnormalised, NaN beyond n/2+1) via a single O(n log n) rustfft spectrum helper; proven correct and fast on a prime n=100003 and Parseval-correct (unnormalised) on n=131072"
    requirement: "FEAT-03"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/src/features.rs#test_fft_features, test_fft_coefficient_nan_beyond_available_bins, test_one_sided_spectrum_prime_n_cosine_peak, test_one_sided_spectrum_parseval_unnormalised, test_spectrum_degenerate_inputs_no_panic"
        status: pass
      - kind: integration
        ref: "crates/anofox-fcst-core/tests/tsfresh_features_reference.rs (tsfresh_parity_* x9 series, pending_features_still_mismatch ratchet)"
        status: pass
      - kind: integration
        ref: "test/sql/ts_features_reference.test (fft_coefficient_0_real/1_imag/3_abs block, 15 assertions across ts_features_by/ts_features/ts_features_agg)"
        status: pass
    human_judgment: false
  - id: D2
    description: "spectral_centroid/spectral_variance follow tsfresh fft_aggregated on the one-sided |rfft(x)| magnitude spectrum (not the two-sided power spectrum); NaN for an all-zero series, 0.0 for a constant series"
    requirement: "FEAT-03"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/src/features.rs#test_spectrum_degenerate_inputs_no_panic"
        status: pass
      - kind: integration
        ref: "crates/anofox-fcst-core/tests/tsfresh_features_reference.rs (all_zero_n50/constant_n30 fixtures within tsfresh_parity_*)"
        status: pass
      - kind: integration
        ref: "test/sql/ts_features_reference.test (spectral_centroid/variance block, same 15-assertion group)"
        status: pass
    human_judgment: false
  - id: D3
    description: "rustfft is a direct anofox-fcst-core dependency resolving to the already-locked 6.4.1 (Cargo.lock still holds exactly one rustfft package); native and WASM builds both succeed"
    requirement: "FEAT-03"
    verification:
      - kind: other
        ref: "grep -c 'name = \"rustfft\"' Cargo.lock == 1; cargo check --target wasm32-unknown-emscripten -p anofox-fcst-core (Finished, no errors)"
        status: pass
    human_judgment: false
  - id: D4
    description: "Full project DoD: cargo fmt/clippy -D warnings/cargo test --workspace green; make release + make test_release green with a higher assertion count than 14-02's baseline; FFI header byte-identical; no mapping.json row carries pending:14-03"
    verification:
      - kind: other
        ref: "make test_release: 1899 assertions in 31 test cases (14-02 baseline 1884/31); cargo fmt --all -- --check; cargo clippy --all-targets --all-features -- -D warnings; cargo test --workspace (339 lib tests, all integration suites green); git diff --quiet -- src/include/anofox_fcst_ffi.h; mapping.json pending:14-03 count == 0"
        status: pass
    human_judgment: false

duration: ~50 min
completed: 2026-10-11
status: complete
---

# Phase 14 Plan 3: FFT/Spectral Parity via rustfft Summary

**fft_coefficient_* and spectral_centroid/variance are now numpy-rfft-exact and O(n log n) via a direct rustfft dependency, replacing the old O(n^2) naive DFT normalised by n and the two-sided power spectrum.**

## Performance

- **Duration:** ~50 min
- **Started:** 2026-10-11T21:08:00Z (approximate)
- **Completed:** 2026-10-11T21:58:44Z
- **Tasks:** 2 completed
- **Files modified:** 6
- **Commits:** 3

## Accomplishments

- Added `rustfft = "6.4"` as a direct dependency of `anofox-fcst-core` (D-07) — it was already compiled into every native and WASM build transitively via `fdars-core`'s mandatory `rustfft = "6.2"` dependency, so this installed no new package; `Cargo.lock` still holds exactly one `rustfft` entry (`6.4.1`).
- Replaced the `O(n^2)` naive DFT (normalised by `n`) and the two-sided power-spectrum helper with `fn one_sided_spectrum` (rustfft `FftPlanner::<f64>::plan_fft_forward` + truncate to `n/2+1` bins — bit-for-bit the numpy `rfft` bins for real input) and `fn fft_aggregated_moments` (tsfresh `fft_aggregated`'s `get_moment` form, no epsilon guard). Both `fft_coefficient_*` and `spectral_centroid`/`spectral_variance` now read the SAME spectrum, computed once per `extract_features` call.
- `fft_coefficient_{0..9}_{real,imag,abs}` are now unnormalised (`fft_coefficient_0_real` on `short_n20`: `55.537`, previously the series mean `2.7768499999999996`) and correctly `NaN` beyond the `n/2+1` bins a real-input FFT actually produces (e.g. `tiny_n8`, `n=8`: bins 5–9 `NaN`, not just 8–9 as the old `O(n^2)` DFT's bin count implied).
- `spectral_centroid`/`spectral_variance` now follow tsfresh's `fft_aggregated` definition on the ONE-sided magnitude spectrum (`short_n20`: `2.740689544749791` / `11.264706553070855`, previously `2.016904841106497` / `22.834066212475133` over the full two-sided power spectrum); `all_zero_n50` correctly yields `NaN` (`0.0/0.0`), `constant_n30` yields `0.0`.
- **All 31 `pending:14-03` mapping.json rows flipped to `match`** with no tolerance override anywhere — rustfft's evaluation order agrees with numpy/tsfresh within the default `1e-9 + 1e-9*|ref|` tolerance on every one of the 9 fixture series, confirmed by the `pending_features_still_mismatch` ratchet test.
- Task 2's three new behavior tests (prime-length `n=100003` cosine spectrum, `n=131072` unnormalised-Parseval identity, degenerate constant/all-zero/`n<=3` inputs) all passed immediately against Task 1's implementation — documented below as a deviation from the literal RED-GREEN cycle, not a correctness gap.
- WASM build check (`cargo check --target wasm32-unknown-emscripten -p anofox-fcst-core`) finishes clean with the new direct `rustfft` dependency.
- `test/sql/ts_features_reference.test` gained 15 new non-json-gated assertions (`fft_coefficient_0_real`/`1_imag`/`3_abs`, `spectral_centroid`/`spectral_variance` on `short_n20` across `ts_features_by`/`ts_features`/`ts_features_agg`). `make test_release` now reports 1899 assertions (up from the 1884 baseline after 14-02).
- CHANGELOG Phase 14 before/after table gained 5 rows for the FFT/spectral family plus an explanatory sentence on the normalisation/complexity/one-sided-spectrum fixes.

## Task Commits

Each task was committed atomically:

1. **Task 1: Tracer — rustfft one-sided spectrum end-to-end** - `7f3ba71` (feat)
2. **Task 2: large-prime-n, Parseval, and degenerate-input spectrum tests** - `d1f0f6f` (test)
3. **Task 2: CHANGELOG before/after rows for the FFT/spectral family** - `4600392` (docs)

_Note: Task 1 (`type="tracer"`) is production-quality, committed like `type="auto"` — no RED/GREEN split. Its tracer feedback gate (re-running `<verify>` end-to-end) passed, so execution continued straight to Task 2 without a checkpoint (interactive, `human_verify_mode: end-of-phase`, the tracer's `<verify>` carries only `<automated>` — row 3 of the checkpoints.md precedence table). Task 2 (`tdd="true"`) produced only a `test(14-03)` commit, not a `feat(14-03)` + `test(14-03)` pair — see TDD Gate Compliance and Deviations below for why._

## TDD Gate Compliance

Task 2 carries `tdd="true"`, but its three behavior tests (`test_one_sided_spectrum_prime_n_cosine_peak`, `test_one_sided_spectrum_parseval_unnormalised`, `test_spectrum_degenerate_inputs_no_panic`) all passed on the **first run**, with zero implementation changes, against the `one_sided_spectrum`/`fft_aggregated_moments` functions Task 1 had already shipped. This is the TDD error-handling rule's "unexpected GREEN in RED phase" case — investigated, not skipped: the feature genuinely already exists, by this plan's own `D-16` structure (Task 1 = tracer ships the full, correct implementation end-to-end through SQL; Task 2 = additional hardening tests for cases Task 1's own unit tests didn't cover, plus the WASM check, CHANGELOG, and full DoD). No RED commit precedes the test commit because there was never a genuinely failing state to capture — the alternative (writing a deliberately-broken intermediate implementation just to manufacture a RED commit) would have been theater, not design feedback. `gsd_run check tdd-red-evidence` was not invoked for the same reason noted in 14-01/14-02: it only parses `node --test`'s TAP output, not cargo's.

| Gate | Status | Commit |
|------|--------|--------|
| RED | N/A — tests passed on first run (investigated, documented above) | — |
| GREEN | N/A — no implementation change needed | — |
| TEST | Present | `d1f0f6f` |

## Files Created/Modified

- `crates/anofox-fcst-core/Cargo.toml` - direct `rustfft = "6.4"` dependency
- `Cargo.lock` - one new dependency edge (anofox-fcst-core -> rustfft); still exactly one `rustfft` package (6.4.1)
- `crates/anofox-fcst-core/src/features.rs` - `fn one_sided_spectrum`, `fn fft_aggregated_moments` (replacing `simple_dft`/`spectral_features`), rewritten FFT/spectral block in `extract_features`, updated `test_fft_coefficient_nan_beyond_available_bins` (now asserts bins 5–9 NaN for n=8, not just 8–9), 3 new Task 2 behavior tests
- `validation/reference/fixtures/tsfresh_features/mapping.json` - 31 rows flipped `pending:14-03` -> `match`, definitions rewritten to drop the stale bug description
- `test/sql/ts_features_reference.test` - 15 new assertions (fft_coefficient_0_real/1_imag/3_abs, spectral_centroid/variance block)
- `CHANGELOG.md` - 5 new before/after rows + explanatory sentence for the FFT/spectral family

## Decisions Made

- `one_sided_spectrum` and `fft_aggregated_moments` are two small, separate helpers rather than one combined function — `fft_coefficient_*` needs the raw complex spectrum (real/imag/abs per bin) while the spectral moments only need magnitudes, but both MUST read the identical spectrum so a future change to one can't silently diverge from the other.
- `mapping.json` row `definition` fields for all 31 flipped rows were rewritten, not just the `status` field, to stop documenting a bug ("current Rust is an O(n^2) naive DFT...") that no longer exists — keeps the mapping file accurate as living documentation of the feature contract, not a historical bug log.
- No per-feature `tolerance` override was added anywhere in this family (D-10 allows one for FFT evaluation-order noise) — the default `1e-9 + 1e-9*|ref|` tolerance held on every fixture series without exception, so adding an override would have been unjustified laxity.

## Deviations from Plan

### Auto-fixed Issues

None — Rules 1–3 did not trigger. The one notable departure from a literal reading of the plan is Task 2's TDD cycle, documented above under "TDD Gate Compliance" rather than as a Rule 1–4 deviation, since it required no code fix: investigated per the TDD error-handling rule, found to be this plan's own intended structure (D-16 splits the full implementation into Task 1 and hardening tests into Task 2), and proceeded with a `test(14-03)`-only commit.

---

**Total deviations:** 0 auto-fixed. **Impact:** None — plan executed as written; the TDD-cycle note above documents expected behavior given the plan's own task split, not a correction.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- The FEAT-03 family (31 of 31 rows from the 14-01 inventory) is fully closed: `mapping.json` has zero `pending:14-03` rows, confirmed by `tsfresh_features_reference`'s ratchet test.
- 14-04's pending inventory (6 rows: `lempel_ziv_complexity`, `benford_correlation`, `binned_entropy`, `sample_entropy`, `permutation_entropy`, `approximate_entropy`) is untouched by this plan and ready to proceed as planned — confirmed unchanged in this run's `pending_features_still_mismatch` output (all 6 still mismatch on every fixture series they were expected to).
- No blockers.

## Self-Check: PASSED

- Files exist on disk: `crates/anofox-fcst-core/Cargo.toml`, `Cargo.lock`, `crates/anofox-fcst-core/src/features.rs`, `validation/reference/fixtures/tsfresh_features/mapping.json`, `test/sql/ts_features_reference.test`, `CHANGELOG.md` — all FOUND.
- Commits exist in `git log`: `7f3ba71`, `d1f0f6f`, `4600392` — all FOUND.
- Re-ran every task's `<acceptance_criteria>` and the plan-level `<verification>` block: all pass (`tsfresh_features_reference` 12/12 green with 0 `pending:14-03` rows; `cargo fmt`/`clippy -D warnings`/`cargo test --workspace` green; WASM `cargo check` finished clean; `make release` + `make test_release` green at 1899 assertions, up from 1884; FFI header byte-identical).

---
*Phase: 14-tsfresh-feature-parity*
*Completed: 2026-10-11*
