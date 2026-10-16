---
phase: 15-detection-decomposition-on-fdars-0-47
plan: 01
subsystem: forecasting-core
tags: [fdars-core, rust, ffi, detrend, decomposition, seasonality, duckdb-extension]

requires: []
provides:
  - "fdars-core bumped 0.3.0 -> 0.47.0 (native [parallel], WASM [js], no linalg)"
  - "single_series_matrix(): the one reviewed FdMatrix construction site in anofox-fcst-core"
  - "ts_detrend / ts_detrend_by polyfit-exact for linear/quadratic/cubic, strict method names"
  - "ts_classify_seasonality(_by) and ts_detect_amplitude_modulation non-trivial (B2 fixed)"
  - "ts_decompose_seasonal non-trivial, strict method/period validation"
  - "ffi_error_code() FFI helper reused by later Phase 15 plans"
  - "numpy_detrend/polyfit_cases.json fixture + detrend_reference.rs parity test"
  - "test/sql/ts_detection_reference.test non-json-gated SQL assertion file (15-01 section)"
  - "Phase 15 docs-snippet harness fork (.planning/phases/15-.../snippets/)"
affects: [15-02, 15-03, 15-04, 15-05]

actuals:
  tokens: 46571
  tasks: 3
  commits: 2

tech-stack:
  added: ["fdars-core 0.47.0"]
  patterns:
    - "single FdMatrix construction helper per crate (single_series_matrix), never FdMatrix::from_slice directly at call sites"
    - "ffi_error_code() centralizes InvalidParameter/InvalidInput -> ErrorCode::InvalidInput mapping at the FFI boundary"
    - "strict FromStr with type Err = ForecastError naming every valid method, replacing silent unwrap_or_default() fallbacks"

key-files:
  created:
    - validation/reference/python/gen_detrend_reference.py
    - validation/reference/fixtures/numpy_detrend/polyfit_cases.json
    - crates/anofox-fcst-core/tests/detrend_reference.rs
    - test/sql/ts_detection_reference.test
    - .planning/phases/15-detection-decomposition-on-fdars-0-47/snippets/ (run_snippets.py, BASE, fixtures.sql, expected_errors.txt, synopsis.txt, pass-list-15-01.tsv)
  modified:
    - Cargo.toml
    - crates/anofox-fcst-core/Cargo.toml
    - crates/anofox-fcst-core/src/detrending.rs
    - crates/anofox-fcst-core/src/seasonality.rs
    - crates/anofox-fcst-core/src/periods.rs
    - crates/anofox-fcst-core/src/peaks.rs
    - crates/anofox-fcst-ffi/src/lib.rs
    - src/table_functions/ts_detrend.cpp
    - src/macros/ts_macros.cpp
    - validation/reference/README.md
    - CHANGELOG.md
    - docs/api/05a-decomposition.md
    - plugins/anofox-forecast/skills/anofox-forecast-detection/SKILL.md
    - plugins/anofox-forecast/skills/anofox-forecast-data-prep/SKILL.md

key-decisions:
  - "Task 1 package-legitimacy checkpoint resolved by prior user approval (2026-10-12, recorded verbatim below) -- executor did not stop, verified metadata, continued directly to Task 2 per orchestrator instruction."
  - "Basis-conversion for polynomial coefficients (divide by (n-1)^k) applied only inside detrend_polynomial for degree >= 2, not in the shared From<FdarsTrendResult> impl, since fdars's degree==1 path delegates to detrend_linear (already raw-index basis) and detrend_diff's 'coefficients' field means something else entirely (initial values, not polynomial powers)."
  - "DecomposeMethod::from_str strictness deferred from Task 2 to Task 3 exactly as the plan specified -- Task 2 only tightened DetrendMethod."
  - "'Before' CHANGELOG values for ts_classify_seasonality / ts_detect_amplitude_modulation / ts_decompose_seasonal were reproduced from fdars-core 0.3.0's own cached source (its m<4 dimension guard on the swapped call), not re-run against the pre-bump binary -- the binary was already rebuilt with the fix before this table was written. ts_detrend's before-values (linear/cubic trend, method string) were captured live from the pre-bump build per Task 2 step 1, before any rebuild."

patterns-established:
  - "Call-order pin tests: a test asserting a numeric correctness property whose only way to silently regress is a (n, m) argument swap, with a doc comment naming the pitfall explicitly (periods.rs, three new tests for the fdars functions NOT migrated to FdMatrix)."

requirements-completed: [DEP-03, WRAP-01]

coverage:
  - id: D1
    description: "fdars-core bumped 0.3 -> 0.47 ([parallel] native, [js] WASM, no linalg); anofox-regression 0.4.0 duplicate removed from Cargo.lock; WASM cargo check green for anofox-fcst-core and anofox-fcst-ffi"
    requirement: "DEP-03"
    verification:
      - kind: unit
        ref: "cargo tree --workspace | grep -c 'anofox-regression v0.4.0' == 0"
        status: pass
      - kind: other
        ref: "cargo check --target wasm32-unknown-emscripten -p anofox-fcst-core -p anofox-fcst-ffi"
        status: pass
    human_judgment: false
  - id: D2
    description: "ts_detrend / ts_detrend_by match numpy polyfit to 1e-8 for linear/quadratic/cubic; unknown method names raise an error naming every valid choice instead of silently falling back to auto"
    requirement: "WRAP-01"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/tests/detrend_reference.rs#polyfit_cases_match_numpy_for_every_degree"
        status: pass
      - kind: integration
        ref: "test/sql/ts_detection_reference.test (15-01 detrend section)"
        status: pass
    human_judgment: false
  - id: D3
    description: "ts_classify_seasonality(_by) and ts_detect_amplitude_modulation return non-trivial, correct results (B2 fixed): a strong sine classifies stable_seasonal/is_seasonal with strength ~1.0; white noise is non_seasonal; amplitude modulation distinguishes stable vs emerging"
    requirement: "WRAP-01"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/src/seasonality.rs#test_classify_seasonality_strong_sine_is_stable_seasonal, #test_classify_seasonality_white_noise_is_non_seasonal, #test_detect_amplitude_modulation_constant_amplitude_is_stable, #test_detect_amplitude_modulation_growing_amplitude_is_emerging"
        status: pass
      - kind: integration
        ref: "test/sql/ts_detection_reference.test (15-01 seasonality/amplitude section)"
        status: pass
    human_judgment: false
  - id: D4
    description: "ts_decompose_seasonal returns non-trivial components (constant series trend exactly 1.0, seasonal ~0; additive/multiplicative identities hold); unknown method and non-positive period raise SQL errors instead of returning NULL"
    requirement: "WRAP-01"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/src/detrending.rs#test_decompose_constant_series_trend_is_exact_and_seasonal_is_zero, #test_decompose_additive_reconstructs_input_and_recovers_seasonal, #test_decompose_multiplicative_reconstructs_input, #test_decompose_method_unknown_name_errors_naming_valid_methods"
        status: pass
      - kind: integration
        ref: "test/sql/ts_detection_reference.test (15-01 decompose section)"
        status: pass
    human_judgment: false
  - id: D5
    description: "The three fdars 0.47 functions not migrated to FdMatrix (estimate_period_acf, estimate_period_regression, detect_multiple_periods) keep their correct call order, pinned by regression tests naming the swap risk"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/src/periods.rs#test_estimate_period_acf_call_order_pin, #test_estimate_period_regression_call_order_pin, #test_detect_multiple_periods_call_order_pin"
        status: pass
    human_judgment: false
  - id: D6
    description: "cargo fmt / clippy / cargo test --workspace clean; make test_release assertion count rises (1935 -> 1963); FFI header byte-identical (no unintended FFI change)"
    verification:
      - kind: other
        ref: "cargo fmt --all -- --check; cargo clippy --all-targets --all-features -- -D warnings; cargo test --workspace; make test_release; git diff --quiet -- src/include/anofox_fcst_ffi.h"
        status: pass
    human_judgment: false
  - id: D7
    description: "Docs and skills updated: stale detrend/classify limitation notes removed, real method sets and error behavior documented, new ts_decompose_seasonal doc section; Phase 15 snippet harness forked with 0 FAIL rows"
    verification:
      - kind: other
        ref: ".planning/phases/15-detection-decomposition-on-fdars-0-47/snippets/pass-list-15-01.tsv (0 FAIL rows, exit 0)"
        status: pass
    human_judgment: false

duration: 42min
completed: 2026-10-12
status: complete
---

# Phase 15 Plan 1: fdars-core 0.47 bump + detrend/classify/decompose tracer Summary

**fdars-core 0.3 -> 0.47 bump fixes the B1/B2 swapped-FdMatrix-dimension bug: `ts_detrend`
now matches numpy `polyfit` to 1e-8, `ts_classify_seasonality` correctly labels a strong sine
as seasonal (was `non_seasonal`/`NaN`), and `ts_decompose_seasonal` returns a real
trend/seasonal split (was all-zero/no-op) — all routed through one reviewed
`single_series_matrix()` helper, with strict method-name parsing replacing every silent
`auto`/`additive` fallback.**

## Performance

- **Duration:** ~42 min
- **Started:** 2026-10-12T20:20:59Z (approx, per STATE.md `last_updated` at phase start)
- **Completed:** 2026-10-12T21:02:17Z
- **Tasks:** 3 (Task 1 checkpoint pre-resolved, Task 2 tracer, Task 3 expansion)
- **Files modified:** 25 (across both commits)

## Task 1: Package-legitimacy gate (pre-resolved)

Per the orchestrator's instructions, this checkpoint was already resolved by the user on
2026-10-12: **"Approved"** — shown fdars-core 0.47.0 on crates.io (published, repository
`github.com/sipemu/fdars`, author "Simon Muller", MIT license), `default = ["parallel"]`,
`linalg = ["faer", "anofox-regression"]` (the only feature pulling in `anofox-regression`/
`faer`), and the native/WASM feature split. The executor re-printed this metadata for the
record before touching any file:

```
[package]
name = "fdars-core"
version = "0.47.0"
authors = ["Simon Muller"]
license = "MIT"
repository = "https://github.com/sipemu/fdars"

[features]
default = ["parallel"]
dhat-heap = []
js = ["getrandom/js"]
linalg = ["faer", "anofox-regression"]
parallel = ["rayon"]
serde = ["dep:serde", "dep:serde_json"]
```
Full dependency list with `["parallel"]`: rayon (optional, added), nalgebra/num-complex/
rand/rand_distr/rustfft (always-on, not optional), getrandom (wasm32 only, `["js"]`).

`git status --porcelain -- Cargo.toml Cargo.lock crates` printed nothing before this task
(no file changed) — confirmed. Proceeded directly to Task 2 per the orchestrator's
`<checkpoint_resolution>` instruction; no checkpoint was returned.

## Accomplishments

- Bumped fdars-core 0.3.0 -> 0.47.0 (`Cargo.toml`, `crates/anofox-fcst-core/Cargo.toml`);
  native features `["parallel"]` (dropped `"linalg"`), WASM `["js"]` unchanged. Resolved via
  `cargo update -p fdars-core` only — `anofox-forecast` stayed 0.15.10, `anofox-regression`
  stayed 0.5.15 (the project's own pin), and the duplicate `anofox-regression 0.4.0` /
  `faer` pulled in transitively by 0.3's `linalg` feature is gone from `Cargo.lock`.
- New `single_series_matrix()` in `detrending.rs` is the ONLY `FdMatrix::from_slice` call
  site in the crate (grep-verified: 1), always `(values, 1, n)`. Every fdars-backed call in
  `detrending.rs`, `seasonality.rs`, `periods.rs` (the 2-arg `estimate_period_fft` call
  sites only), and `peaks.rs` now builds its matrix through this helper.
- Fixed B1 (7 call sites in `detrending.rs` that passed `(values, n, 1, ...)` — backwards
  for a single series) and B2 (`classify_seasonality`, `detect_amplitude_modulation_wavelet`
  in `seasonality.rs`).
- D-02/B3: `DetrendMethod` gained `Quadratic`/`Cubic`; `FromStr` is strict
  (`type Err = ForecastError`), naming every valid method on an unknown name. Same for
  `DecomposeMethod` (Task 3). `detrend_polynomial` converts fdars's normalised-time
  (`t/(n-1)`) coefficients back to the raw 0-based index basis for degree >= 2.
- New `ffi_error_code()` FFI helper (InvalidParameter/InvalidInput -> `ErrorCode::InvalidInput`,
  else `ComputationError`); used by `anofox_ts_detrend` (Task 2) and `anofox_ts_decompose`
  (Task 3); reusable by later Phase 15 plans.
- C++ `ts_detrend`/`ts_decompose_seasonal` now raise `InvalidInputException` on
  `INVALID_INPUT` instead of returning a silent `NULL` row (precedent:
  `ts_data_quality.cpp`).
- `validation/reference/fixtures/numpy_detrend/polyfit_cases.json` (new generator
  `gen_detrend_reference.py`): numpy `polyfit` degree 1/2/3 fitted values, coefficients, RSS
  for 4 seeded series (cubic40, quadratic60, linear50, const30). Deterministic regeneration
  verified (`git diff --exit-code` empty after a second run).
- `detrend_reference.rs`: parity test asserting both the direct entry points
  (`detrend_linear`/`detrend_polynomial`) and the named `DetrendMethod` dispatch against the
  fixture — cubic40 degree-3 `fitted[0] = 3.154511161980401` matches within 1e-8 (previously
  every trend value was `0.0`).
- `test/sql/ts_detection_reference.test` (new, non-json-gated): 28 assertions across detrend
  (14, Task 2) and classify/amplitude/decompose (14, Task 3) — `grep -c 'require json'` is 0.
- Task 3 behavior-block tests replace every "runs without error" placeholder test in
  `seasonality.rs`/`detrending.rs` with real value assertions (see `coverage` above); three
  new call-order pin tests in `periods.rs` document the swap risk still latent in the three
  fdars functions not migrated to `FdMatrix`.
- WASM: `cargo check --target wasm32-unknown-emscripten` passes for both
  `anofox-fcst-core` and `anofox-fcst-ffi`; `cargo tree --workspace` confirms zero
  `anofox-regression v0.4.0` anywhere.
- CHANGELOG: new Breaking-changes bullet with a 9-row before/after table (the "before"
  values for classify/amplitude/decompose were reproduced from fdars-core 0.3.0's own cached
  source — its `m < 4` dimension guard on the swapped call — rather than a live re-run of
  the pre-bump binary, which was already rebuilt by the time the table was written). Also
  measured and recorded the `O(n^2)` cost of `'loess'`/`'auto'` on a 20000-point series
  (~2.5s wall for `'auto'`, ~0.27s for `'loess'` alone, on this machine — previously
  instantaneous since the swapped-argument bug returned a no-op before fdars did any real
  work).
- Docs/skills: removed every stale "fix planned for v0.10.0, Phase 15" / argument-order /
  no-op note for detrend/classify across `docs/api/05a-decomposition.md` and both skill
  files (the 3 MSTL notes, owned by plan 15-04, are untouched — verified via grep, count 3);
  documented the real method set, strict-parse error, coefficient basis, and `O(n^2)` cost;
  added a `ts_decompose_seasonal` doc section + skill entry.
- Forked the Phase 14 docs-snippet harness into
  `.planning/phases/15-detection-decomposition-on-fdars-0-47/snippets/` (`BASE` =
  `1d871c662651b2d8670300d8e48c47b0169b8e4c`); `pass-list-15-01.tsv` has 0 FAIL rows for the
  touched docs/skills (1 signature-only pseudo-SQL block correctly routed to `SYNOPSIS` via
  `synopsis.txt`, matching the project's established convention for non-executable
  `Signature:` blocks).

## Task Commits

Each task was committed atomically:

1. **Task 1: Package-legitimacy gate** — no commit (verification-only; pre-resolved by
   user approval, no file changed).
2. **Task 2: Tracer — fdars 0.47 bump + FdMatrix helper + strict detrend methods** —
   `b528af1` (feat)
3. **Task 3: Classify/amplitude/decompose non-trivial + call-order pins + WASM/CHANGELOG/
   docs sweep** — `b3755ae` (feat)

_Tracer feedback gate (after Task 2): `HUMAN_VERIFY_MODE=end-of-phase`, `<verify>` was
automated-only, auto mode was NOT active (`workflow.auto_advance: false`,
`_auto_chain_active: false`) — per the executor's tracer rule chain this re-runs the
tracer's `<verify>` end-to-end and continues without a checkpoint on success. Re-ran; all
commands passed; logged `⚡ Tracer verified end-to-end — expanding` and proceeded directly
to Task 3._

**Note on `git rev-list --count` (commits ledger, #3968):** `git rev-list --count
bfaac68c36c045b2e6e230e33520e374d4140264..HEAD` reports **5**, not 2 — this branch had 3
concurrent sibling-plan commits interleaved between this plan's two task commits
(`debc77c` docs(16), `19ab928`/`15bb49c` docs(17)), from other planning agents working the
same branch concurrently, exactly as flagged as expected in this executor's
`<sequential_execution>` instructions ("Other planning agents may commit
`.planning/phases/16-*` and `17-*` docs concurrently"). `actuals.commits: 2` above reports
this plan's own commits only; `plan_head_before: bfaac68c36c045b2e6e230e33520e374d4140264`
is recorded for the same-instrument check, with this explanation so the 5-vs-2 discrepancy
is not mistaken for an uncommitted-work gap.

**Plan metadata:** (this commit, docs: complete 15-01 plan)

## Files Created/Modified

**Created:**
- `validation/reference/python/gen_detrend_reference.py` — numpy polyfit fixture generator
- `validation/reference/fixtures/numpy_detrend/polyfit_cases.json` — fixture data (4 series x 3 degrees)
- `crates/anofox-fcst-core/tests/detrend_reference.rs` — Rust parity test against the fixture
- `test/sql/ts_detection_reference.test` — non-json-gated SQL value assertions (Phase 15, 15-01 section)
- `.planning/phases/15-detection-decomposition-on-fdars-0-47/snippets/{run_snippets.py,BASE,fixtures.sql,expected_errors.txt,synopsis.txt,pass-list-15-01.tsv}` — docs-snippet harness fork

**Modified:**
- `Cargo.toml`, `crates/anofox-fcst-core/Cargo.toml` — fdars-core 0.3 -> 0.47, feature list
- `crates/anofox-fcst-core/src/detrending.rs` — `single_series_matrix`, strict `DetrendMethod`/`DecomposeMethod`, basis-converted polynomial coefficients, new behavior tests
- `crates/anofox-fcst-core/src/seasonality.rs` — all fdars calls through the matrix helper (B2 fix), real value-assertion tests replacing placeholders
- `crates/anofox-fcst-core/src/periods.rs` — `estimate_period_fft` 2-arg call sites, 3 new call-order pin tests
- `crates/anofox-fcst-core/src/peaks.rs` — mechanical `FdMatrix` wrap (compile fix only; full rewrite is plan 15-02)
- `crates/anofox-fcst-ffi/src/lib.rs` — `ffi_error_code()`, strict method parsing in `anofox_ts_detrend`/`anofox_ts_decompose`
- `src/table_functions/ts_detrend.cpp` — raise `InvalidInputException` on `INVALID_INPUT`, corrected example strings
- `src/macros/ts_macros.cpp` — `ts_detrend_by` example method name/comment
- `validation/reference/README.md` — `numpy_detrend` family row, tolerance rationale, regeneration command
- `CHANGELOG.md` — Phase 15 Breaking-changes bullet + before/after table
- `docs/api/05a-decomposition.md` — detrend method docs, new `ts_decompose_seasonal` section, removed stale limitation notes
- `plugins/anofox-forecast/skills/anofox-forecast-detection/SKILL.md` — same doc sweep
- `plugins/anofox-forecast/skills/anofox-forecast-data-prep/SKILL.md` — `ts_detrend_by` note corrected

## Decisions Made

See `key-decisions` in frontmatter.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] `#[non_exhaustive]` enums from fdars-core 0.47 needed catch-all match arms**
- **Found during:** Task 2 (compiling `seasonality.rs` against the bumped fdars-core)
- **Issue:** fdars-core 0.47 marked `SeasonalType`, `ModulationType`, and `ChangeType`
  `#[non_exhaustive]`; the existing exhaustive `match` conversions in `seasonality.rs`
  (`From<FdarsSeasonalType> for SeasonalType`, etc.) no longer compile against a
  non-exhaustive foreign enum without a wildcard arm.
- **Fix:** Added a `_ => <conservative default>` arm to each of the three `From` impls
  (falls back to `NonSeasonal`/`Cessation`), with a comment explaining why.
- **Files modified:** `crates/anofox-fcst-core/src/seasonality.rs`
- **Verification:** `cargo build -p anofox-fcst-core --lib` clean; `cargo test` green.
- **Committed in:** `b528af1`

**2. [Rule 1 - Bug] clippy `needless_range_loop` on the new decompose-identity tests**
- **Found during:** Task 3 DoD (`cargo clippy --all-targets --all-features -- -D warnings`)
- **Issue:** `for i in 0..n { ... values[i] ... }` loops in the new
  `test_decompose_additive_reconstructs_input_and_recovers_seasonal` /
  `test_decompose_multiplicative_reconstructs_input` tests triggered
  `clippy::needless_range_loop`.
- **Fix:** Rewrote both loops as `for (i, &want) in values.iter().enumerate()`.
- **Files modified:** `crates/anofox-fcst-core/src/detrending.rs`
- **Verification:** `cargo clippy --all-targets --all-features -- -D warnings` clean.
- **Committed in:** `b3755ae`

**3. [Rule 3 - Blocker] New `ts_decompose_seasonal` doc example/signature block failed the snippet harness**
- **Found during:** Task 3 step 9 (running the forked snippet harness)
- **Issue:** The new `ts_decompose_seasonal` section added two problems the harness caught:
  (a) its `Signature:` pseudo-SQL line (`ts_decompose_seasonal(values DOUBLE[], ...) →
  STRUCT(...)`) is not valid executable SQL — this matches the project's own established
  convention elsewhere in the same file (`ts_mstl_decomposition_by`, `ts_conformal_quantile`
  in a sibling doc, etc. all do the same), but since it was NEW text it was in-scope for
  execution and failed; (b) the runnable example used `LIST(value ORDER BY date)` against
  the `monthly_sales` fixture table, which actually has columns `product_id, date,
  quantity` — no `value` column.
- **Fix:** (a) added the signature line's key to `synopsis.txt` (the harness's existing
  mechanism for routing known-non-executable `Signature:` blocks to `SYNOPSIS` instead of
  `FAIL`, used throughout the rest of this same file); (b) corrected the example to
  `LIST(quantity ORDER BY date) ... WHERE product_id = 1`, verified by direct execution
  against the fixtures.
- **Files modified:** `docs/api/05a-decomposition.md`,
  `.planning/phases/15-detection-decomposition-on-fdars-0-47/snippets/synopsis.txt`
- **Verification:** `python3 run_snippets.py --out pass-list-15-01.tsv <files>` → 0 FAIL.
- **Committed in:** `b3755ae`

---

**Total deviations:** 3 auto-fixed (2x Rule 1 - bug, 1x Rule 3 - blocker).
**Impact:** All three were mechanical compile/lint/harness fixes with no behavioral scope
creep; none changed the plan's intended numerical results.

## Known Stubs

None.

## Issues Encountered

None beyond the deviations documented above.

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

- Plan 15-02 (scipy peaks, DET-01) can proceed: `peaks.rs` already compiles against
  fdars-core 0.47 (mechanical `FdMatrix` wrap only); its fdars call will be discarded by
  15-02's full re-implementation regardless.
- `single_series_matrix()` and `ffi_error_code()` are in place in `detrending.rs` /
  `crates/anofox-fcst-ffi/src/lib.rs` for 15-02..15-05 to reuse directly.
- `test/sql/ts_detection_reference.test` and the Phase 15 snippet harness
  (`.planning/phases/15-detection-decomposition-on-fdars-0-47/snippets/`) are both in place
  for every later Phase 15 plan to append sections to.
- No blockers identified for 15-02.

## Self-Check: PASSED

- `crates/anofox-fcst-core/tests/detrend_reference.rs` exists on disk: FOUND
- `test/sql/ts_detection_reference.test` exists on disk: FOUND
- `validation/reference/fixtures/numpy_detrend/polyfit_cases.json` exists on disk: FOUND
- `.planning/phases/15-detection-decomposition-on-fdars-0-47/snippets/pass-list-15-01.tsv` exists on disk: FOUND
- `git log --oneline --all | grep b528af1`: FOUND
- `git log --oneline --all | grep b3755ae`: FOUND
- Re-ran plan-level `<verification>`: Cargo.lock fdars-core 0.47.0 only / anofox-regression 0.5.15 only / anofox-forecast 0.15.10 (PASS); WASM cargo check green both crates (PASS); `detrend_reference` + new unit tests pass, `cargo fmt`/`clippy`/`cargo test --workspace` green (PASS); `ts_detection_reference.test` (28 assertions, 0 json-gate) + `ts_detection_null_policy.test` (24 assertions) both pass (PASS); docs/skills updated, 0 stale notes beyond the 3 MSTL ones, snippet harness 0 FAIL, FFI header unchanged (`header_changed=0`) (PASS).

---
*Phase: 15-detection-decomposition-on-fdars-0-47*
*Completed: 2026-10-12*
