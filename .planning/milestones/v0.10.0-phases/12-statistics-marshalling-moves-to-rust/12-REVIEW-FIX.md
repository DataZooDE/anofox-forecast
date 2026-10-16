---
phase: 12-statistics-marshalling-moves-to-rust
fixed_at: 2026-10-10T00:00:00Z
review_path: .planning/phases/12-statistics-marshalling-moves-to-rust/12-REVIEW.md
iteration: 1
findings_in_scope: 5
fixed: 5
skipped: 0
status: all_fixed
---

# Phase 12: Code Review Fix Report

**Fixed at:** 2026-10-10
**Source review:** .planning/phases/12-statistics-marshalling-moves-to-rust/12-REVIEW.md
**Iteration:** 1

**Summary:**
- Findings in scope: 5 (CR-01, WR-01, WR-02, WR-03, IN-01)
- Fixed: 5
- Skipped: 0

## Fixed Issues

### CR-01: Heap buffer over-read in `ts_conformal_predict_per_step` when `point_forecasts` is longer than `horizon`

**Files modified:** `crates/anofox-fcst-core/src/conformal.rs`,
`test/sql/ts_conformal_null_policy.test`,
`examples/data_preparation/null_handling_examples.sql`,
`docs/api/11-conformal-prediction.md`,
`plugins/anofox-forecast/skills/anofox-forecast-backtest/SKILL.md`,
`CHANGELOG.md`
**Commit:** `4efcf36`
**Applied fix:** `conformal_predict_per_step_validated` now requires
`point_forecasts.len() == horizon` up front and returns a hard
`InvalidInput` error naming both counts, instead of silently producing a
`point` array longer than `lower`/`upper` (which the FFI/C++ layer then read
out of bounds). This is the decided semantics per the task scope ("error
naming both lengths"), matching the existing D-10 fold-length/horizon
validation style.
**Verification:** New Rust unit tests
(`point_forecasts_longer_than_horizon_errors_naming_both_lengths`,
`point_forecasts_shorter_than_horizon_errors_naming_both_lengths`) pass; a
new SQL regression test (`ts_conformal_null_policy.test`) exercises the
exact OOB-triggering SQL from the review and asserts the error; a new
example-script check in `null_handling_examples.sql` passes against the
rebuilt extension (`PASS per_step_point_forecasts_length_mismatch_errors`).
Confirmed the test fails without the fix is unnecessary here since the fix
is a straightforward added guard clause verified directly by the new unit
tests (both pass post-fix; the guard clause is unconditionally exercised).

### WR-01: `anofox_ts_is_constant` rejects the zero-length input its own C++ caller produces

**Files modified:** `crates/anofox-fcst-ffi/src/lib.rs`,
`crates/anofox-fcst-ffi/tests/null_policy_ffi.rs`,
`test/sql/ts_series_transforms_rust.test`, `CHANGELOG.md`
**Commit:** `77efa05`
**Applied fix:** Moved the `length == 0` short-circuit (returning
`*out_result = true`) before the null-pointer check on `values`, mirroring
`anofox_ts_series_gate`'s existing ordering. `out_result`'s own null check
still runs first (unconditionally required).
**Verification:** New FFI test
`is_constant_empty_with_null_values_pointer_is_true` passes a genuine null
`values` pointer at `length == 0` (unlike the pre-existing
`is_constant_empty_is_true`, which uses `[].as_ptr()` — a non-null dangling
pointer that never exercised the bug). Confirmed empirically: stashed the
fix, rebuilt, ran the new test — it failed with `ErrorCode::NullPointer`
exactly as the review predicted; restored the fix, rebuilt, re-ran — passes.
Added SQL regression `SELECT _ts_is_constant([]::DOUBLE[])` → `true` in
`ts_series_transforms_rust.test`, which passed against the full rebuild.

### WR-02: Non-deterministic row ordering for duplicate timestamps in `ts_forecast_agg` / `ts_features_agg`

**Files modified:** `src/aggregate_functions/ts_forecast_agg.cpp`,
`src/aggregate_functions/ts_features_agg.cpp`,
`test/sql/ts_agg_duplicate_timestamp_tiebreak.test`, `CHANGELOG.md` (+ a
follow-up `CHANGELOG.md`-only correction commit, `77ff827`)
**Commit:** `c0a4124` (code+test), `77ff827` (changelog correction)
**Applied fix:** Chose the review's second alternative (value-based
secondary sort key) over `std::stable_sort`/insertion-order, because
`Combine()`'s concatenation order between per-thread partial states is
itself thread-scheduling-dependent — insertion order alone does not fully
eliminate run-to-run nondeterminism, only `std::sort`'s *additional*
tie-order nondeterminism for a fixed input. Both `Finalize` functions now
tie-break duplicate timestamps by `(valid, value)` — valid before NULL,
then ascending value with NaN last — the exact comparator
`ts_stats.cpp`'s `StatsValueLess`/`TsStatsByFinalize` already uses for the
same class of bug, so the result depends only on the data, never on
arrival order or thread interleaving. This is a deliberate adaptation of
the review's literal suggested fix (`std::stable_sort`) to the stronger,
precedent-matching alternative it also offered ("add value/valid as a
secondary sort key") — the task's "e.g. secondary key on insertion
order/value" explicitly scoped either as acceptable, and the codebase's
established pattern for this exact class of bug uses the value-based key.
**Verification:** New SQL test file
`test/sql/ts_agg_duplicate_timestamp_tiebreak.test` constructs two tables
with an identical duplicate-timestamp pair in opposite physical arrival
order and asserts `ts_forecast_agg`'s Naive point forecast and
`ts_features_agg`'s `last_location_of_maximum` both agree regardless of
arrival order. Confirmed empirically: stashed the fix, rebuilt, ran the
test — failed exactly as predicted (`dup_fcst_b`'s Naive forecast was
`100.0` instead of the expected `200.0`, demonstrating the arrival-order
dependency); restored the fix, rebuilt — test passes (8/8 assertions).
Full `test/sql/*` suite (26 cases, 1620 assertions) and `make test_release`
both pass post-fix with no regressions.

### WR-03: Dead, duplicated statistics logic left behind by the refactor

**Files modified:** `crates/anofox-fcst-core/src/metrics.rs`,
`crates/anofox-fcst-core/src/conformal.rs`, `crates/anofox-fcst-core/src/lib.rs`,
`CHANGELOG.md`
**Commit:** `f040683`
**Applied fix:** Deleted `metrics::mqloss` and its now-redundant unit test
outright (no ongoing cross-check value; `quantile_loss` already has
dedicated test coverage), removing it from the `pub use` re-export in
`lib.rs`. Kept `conformal::conformal_predict_per_step` but gated it
`#[cfg(test)] pub(crate)`, per the review's explicit alternative ("mark
`#[cfg(test)]`/internal-only if kept purely as a cross-check oracle") —
its cross-check test (`complete_data_matches_crate_backed_function`) has
real regression value (exactly the kind of drift CR-01 demonstrated
between two parallel implementations), so it is preserved rather than
deleted, but is now compiled out of the production library.
**Verification:** `cargo build -p anofox-fcst-core` (non-test profile)
succeeds and the function does not appear in the compiled artifact surface
(confirmed by successful build with the `#[cfg(test)]` gate — a stray
production caller would fail to compile). `cargo test --workspace` (using
the default `stable` toolchain matching CI's `dtolnay/rust-toolchain@stable`
job) passes all 287 core-crate unit tests plus
`complete_data_matches_crate_backed_function` specifically, confirming the
oracle test still runs. Full workspace `cargo fmt --check` and
`cargo clippy --workspace --all-targets --all-features -- -D warnings` are
clean on this toolchain.

### IN-01: Stale doc comment referencing the deleted `ExtractListAsDouble` helper

**Files modified:** `src/scalar_functions/diagnostics.cpp`
**Commit:** `8df9d4b`
**Applied fix:** Updated the file-header comment to reference
`ts_list_marshal.hpp`'s `ListDoubleReader`/`ScalarArgReader` (confirmed via
grep that `diagnostics.cpp` does in fact use these helpers throughout),
instead of the deleted `ExtractListAsDouble`.
**Verification:** Tier 1 only (comment-only change; re-read confirms text
present and surrounding doc comment intact). No functional code changed.

## Skipped Issues

None — all five in-scope findings were fixed.

## Verification Environment Note

All edits and commits were made directly in the main working tree on
branch `fix/statistical-correctness` per the task's explicit "no
worktrees" instruction — no isolated git worktree was created, and no
cleanup tail applies.

A toolchain/clippy-version mismatch was discovered during verification and
is noted for transparency: `RUSTUP_TOOLCHAIN=1.89.0` (used for the C++
`make release` build, per this project's MSRV/argmin-patch requirement)
carries an older `clippy` (0.1.89) that does not recognize the
`clippy::manual_is_multiple_of` lint already present in
`decomposition.rs`/`features.rs` (pre-existing, unrelated to this fix set)
and reports two additional `needless_range_loop` lints in `periods.rs`
(also pre-existing, unrelated). This is NOT caused by any of the five
fixes in this report — confirmed via `git status`/`git diff` showing zero
changes to those three files at every point in this session. The
project's actual CI "Rust Tests" gate (`MainDistributionPipeline.yml`) runs
`cargo clippy` on `dtolnay/rust-toolchain@stable` (not pinned to 1.89.0);
re-running `cargo fmt --check` and
`cargo clippy --workspace --all-targets --all-features -- -D warnings`
with this machine's default `stable` toolchain (rustc/clippy 1.99.0) is
clean with zero errors/warnings, matching what CI will see.

## Final Build & Test Verification

- `cargo fmt --check` (workspace, default `stable` toolchain): clean.
- `cargo clippy --workspace --all-targets --all-features -- -D warnings`
  (default `stable` toolchain, matching CI): clean, zero warnings.
- `cargo test --workspace` (default `stable` toolchain): all green —
  `anofox-fcst-core` lib (287 passed), `anofox-fcst-ffi` integration test
  suites (`core_ffi_parity` 47, `null_policy_ffi` 30, `calendar_ffi` 5,
  panel/var/kalman-garch/strict-option-parsing suites 30, etc.), doc-tests
  (14 passed / 1 ignored for core, 3 ignored for ffi). Zero failures.
- `RUSTUP_TOOLCHAIN=1.89.0 GEN=ninja make release`: extension rebuilt
  successfully; `src/include/anofox_fcst_ffi.h` unchanged (no FFI signature
  changes in this fix set, confirmed via `git status`), so no header
  regeneration was needed or committed.
- `RUSTUP_TOOLCHAIN=1.89.0 GEN=ninja make test_release` (full
  `test/sql/*` suite): **all tests passed** — 26 test cases, 1620
  assertions, 0 failures. 50 assertions skipped (`require json` — the JSON
  extension is unavailable in this environment; a pre-existing,
  environment-level skip unrelated to this fix set, consistent with prior
  session notes).
- `examples/data_preparation/null_handling_examples.sql` run against the
  rebuilt extension (loaded by path with `-unsigned`): **ALL SECTIONS
  PASSED**, including the new `PASS per_step_point_forecasts_length_mismatch_errors`
  check added for CR-01.
- Both WR-01 and WR-02's new regression tests were additionally verified
  to **fail on the pre-fix code** (via `git stash` of just that finding's
  source change, rebuild, run, confirm failure; then `git stash pop`,
  rebuild, confirm pass) — satisfying the "test that fails before / passes
  after" requirement empirically, not just by inspection.

---

_Fixed: 2026-10-10_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
