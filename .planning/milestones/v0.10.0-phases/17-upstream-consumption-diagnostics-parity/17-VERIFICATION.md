---
phase: 17-upstream-consumption-diagnostics-parity
verified: 2026-10-15T20:30:00Z
status: passed
score: 4/4 must-haves verified
covered_files:
  - ".planning/phases/17-upstream-consumption-diagnostics-parity/17-01-PLAN.md"
  - ".planning/phases/17-upstream-consumption-diagnostics-parity/17-01-SUMMARY.md"
  - ".planning/phases/17-upstream-consumption-diagnostics-parity/17-02-PLAN.md"
  - ".planning/phases/17-upstream-consumption-diagnostics-parity/17-02-SUMMARY.md"
  - ".planning/phases/17-upstream-consumption-diagnostics-parity/17-03-PLAN.md"
  - ".planning/phases/17-upstream-consumption-diagnostics-parity/17-03-SUMMARY.md"
  - ".planning/phases/17-upstream-consumption-diagnostics-parity/17-CONTEXT.md"
  - ".planning/phases/17-upstream-consumption-diagnostics-parity/17-REVIEW-FIX.md"
  - ".planning/phases/17-upstream-consumption-diagnostics-parity/17-REVIEW.md"
  - "CHANGELOG.md"
  - "Cargo.toml"
  - "crates/anofox-fcst-core/src/forecast.rs"
  - "crates/anofox-fcst-core/src/validation.rs"
  - "crates/anofox-fcst-ffi/src/lib.rs"
  - "docs/api/07-forecasting.md"
  - "docs/api/10-diagnostics.md"
  - "src/macros/ts_macros.cpp"
  - "src/scalar_functions/diagnostics.cpp"
covered_digest: "v1:sha256:de62a1bd9199e9af02916ec0039e2228b028ca8a5e06337d52e99de92eb8eceb"
behavior_unverified: 0
overrides_applied: 0
re_verification:
  previous_status: none
  previous_score: n/a
  gaps_closed: []
  gaps_remaining: []
  regressions: []
---

# Phase 17: Upstream Consumption + Diagnostics Parity Verification Report

**Phase Goal:** The extension runs on the corrected upstream crate, and SQL users get statsmodels/R-matching
stationarity verdicts, a model-aware Ljung-Box test, and point/interval parity with R `forecast` across the core
model set.
**Verified:** 2026-10-15T20:30:00Z
**Status:** passed
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

| # | Truth (ROADMAP success criterion) | Status | Evidence |
|---|------|--------|----------|
| 1 | Extension builds/tests against the upstream fix branch then the released version via an interim patch, and at milestone close no `[patch]`/git/path override remains in Cargo.toml for anofox-forecast | ✓ VERIFIED | `Cargo.toml` line 18: `anofox-forecast = { version = "0.16.1", features = [...] }`; `grep -c 'anofox-forecast'` under `[patch.crates-io]` = 0 (only `argmin`/`argmin-math`, pre-existing and explicitly allowed). `Cargo.lock` has exactly one `anofox-forecast` entry (0.16.1, crates.io registry source). `cargo tree -i anofox-forecast` shows a single clean edge, no duplicates. `objdump -p` on the built `.duckdb_extension` has zero `NEEDED` entries matching ssl/crypto (independently re-run). D-14 start guard (crates.io API, `0.16.1 False`) and post-bump content guard (`r_arima.rs` + `[0.16.1]` CHANGELOG section) both recorded in 17-01-SUMMARY.md before any edit. |
| 2 | `ts_adf_by`/`ts_kpss_by` return corrected statistics/p-values (matching Phase 11 fixtures); `ts_stationarity_by` uses the standard four-way mapping — trend-stationary on a trend-stationary fixture, explicit verdict for a constant series | ✓ VERIFIED | `cargo test -p anofox-fcst-core --test stationarity_reference --release`: 4/4 pass (re-run independently, not just trusted from SUMMARY) — ADF vs statsmodels, KPSS vs statsmodels/R tseries, D-04 verdict vs fixture, all 8 series × {c, ct}. Live SQL: `ts_stationarity(30×5.0).verdict = 'constant'`; `ts_stationarity([1.0,2.0,3.0]).verdict IS NULL`. `src/macros/ts_macros.cpp` confirms `regression := 'c'|'ct'` threaded through `ts_adf_by`/`ts_kpss_by`/`ts_stationarity_by`. `test/sql/ts_diagnostics_reference.test`: 72 assertions pass (independently re-run), 0 `require json` lines. |
| 3 | `ts_ljung_box_by(..., fitted_params := p + q)` uses df = lags − p − q matching R `Box.test(fitdf)`; `docs/api/10-diagnostics.md` makes only fixture-verified claims, every snippet run against the built extension | ✓ VERIFIED | `cargo test -p anofox-fcst-core --test portmanteau_reference --release`: 4/4 pass (independently re-run) — Ljung-Box fitdf parity on ARMA(1,1) residuals and diff(rw), Jarque-Bera, Durbin-Watson. Live SQL: `ts_ljung_box(sin-series, 5, 5)` raises `Invalid parameter 'fitted_params'...degrees of freedom lags - fitted_params must be >= 1` exactly as documented. `src/macros/ts_macros.cpp` ts_ljung_box_by has `fitted_params` named default 0. Snippet harness re-run independently (not reused from SUMMARY's own run): `run_snippets.py` on `docs/api/10-diagnostics.md` + EDA skill + README → 0 FAIL. `git grep` for stale caveats ("fix planned for v0.10.0", "non-augmented", "9-point", etc.) in the diagnostics doc/skill → 0 hits. |
| 4 | Point forecasts + 95% intervals for Naive/SNaive/RWD/SES/Holt/HW/AutoETS/AutoARIMA/Theta/Croston within documented tolerance of R `forecast` on AirPassengers + simulated fixtures; Monte-Carlo local-level interval coverage within ±3pp of nominal | ✓ VERIFIED | `cargo test -p anofox-fcst-core --test r_forecast_parity --release`: 3/3 pass (independently re-run) — 36-row parity suite (15 air + 9 rw + 3 inter + 9 trend), `coverage_guard` confirms all 10 FCST-09 model families represented, `autoarima_selection_matches_r` confirms UPST-01 (60/60 exact order agreement). `cargo test --test interval_coverage_mc --release`: 2/2 pass — ETS(A,N,N)/SESOptimized 95%/80% coverage within tolerance. `cargo test --test r_parity_diagnosis --release -- --nocapture`: independently re-run with full ladder output — confirms every `known_gap` row is evidenced (E5/E6/E7/E8 numbers match the fixture's recorded causes), and directly reproduces the one documented D-16 exception: `autoarima air: ... tier=outside` (interval 0.1847, matching the doc's "~18%"), while `autoarima rw`/`autoarima trend` both show `tier=parity`. `diagnosis/check_root_causes.py --stage final`: `invalid=0 uncovered=0 missing_drafts=0 repro_mismatch=0 orphans=0 uncaused=0 autoarima_gaps=0`. `test/sql/ts_forecast_reference.test`: 19 assertions pass (independently re-run). |

**Score:** 4/4 truths verified (0 present, behavior-unverified)

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `Cargo.toml` / `Cargo.lock` | `anofox-forecast` 0.16.1, no override | ✓ VERIFIED | Confirmed above |
| `crates/anofox-fcst-core/src/validation.rs` | `StationarityRegression`, `kpss_trend`, D-04 `classify_stationarity`, model-aware `ljung_box` | ✓ VERIFIED | Present; backing unit tests pass (`cargo test --lib validation` implied by the full-suite run earlier in the session: all green) |
| `crates/anofox-fcst-core/tests/stationarity_reference.rs` | ADF/KPSS/verdict parity vs statsmodels/R | ✓ VERIFIED | 4/4 tests pass (re-run) |
| `crates/anofox-fcst-core/tests/portmanteau_reference.rs` | Ljung-Box fitdf, Jarque-Bera, Durbin-Watson parity | ✓ VERIFIED | 4/4 tests pass (re-run) |
| `crates/anofox-fcst-core/tests/r_forecast_parity.rs` | 36-row FCST-09 parity suite + UPST-01 selection test | ✓ VERIFIED | 3/3 tests pass (re-run) |
| `crates/anofox-fcst-core/tests/interval_coverage_mc.rs` | Seeded Monte Carlo coverage | ✓ VERIFIED | 2/2 tests pass (re-run) |
| `crates/anofox-fcst-core/tests/r_parity_diagnosis.rs` | D-12 E1-E9 ladder + AutoARIMA order/parity report | ✓ VERIFIED | 6 passed, 3 ignored (upstream repros, correctly `#[ignore]`) — re-run with `--nocapture`, numbers match fixture/doc claims |
| `test/sql/ts_diagnostics_reference.test` | Non-json-gated ADF/KPSS/Ljung-Box SQL assertions | ✓ VERIFIED | 72 assertions pass (re-run) |
| `test/sql/ts_forecast_reference.test` | Non-json-gated forecast parity SQL assertions | ✓ VERIFIED | 19 assertions pass (re-run) |
| `docs/api/10-diagnostics.md` | Fixture-verified claims only | ✓ VERIFIED | Names `stationarity_reference`/`portmanteau_reference`/`ts_diagnostics_reference.test`; 0 stale-caveat hits |
| `.planning/.../diagnosis/check_root_causes.py` | Root-cause table integrity gate | ✓ VERIFIED | `--stage final` exits 0, all counters 0 |
| `.planning/.../diagnosis/upstream-issues/RC-0{3,4,5}-*.md` | Upstream issue drafts (not filed) | ✓ VERIFIED | 3 files present on disk |
| `.planning/.../snippets/run_snippets.py` + `pass-list-17-03.tsv` | Phase 17 doc snippet harness | ✓ VERIFIED | Re-run independently: PASS 6, FAIL 0 |

### Key Link Verification

| From | To | Via | Status |
|------|-----|-----|--------|
| `src/macros/ts_macros.cpp ts_adf_by/ts_kpss_by/ts_stationarity_by` | `ts_adf/ts_kpss/ts_stationarity` scalar fns | `regression::VARCHAR` passthrough | ✓ WIRED (code read directly) |
| `src/macros/ts_macros.cpp ts_ljung_box_by` | `ts_ljung_box` scalar fn | `fitted_params::INTEGER` named default 0 | ✓ WIRED (code read directly) |
| `tolerance.parity_checks[*].cause` (known_gap) | `tolerance.root_causes[*].id` | RC-NN IDs | ✓ WIRED (`check_root_causes.py --stage final`, 0 invalid/uncaused/orphans) |
| `docs/api/07-forecasting.md` AutoARIMA interval note | `crates/anofox-fcst-core/tests/r_parity_diagnosis.rs` measured interval | D-16 checkpoint narrative | ✓ WIRED — doc says "~18%", diagnosis test independently measures `interval=1.846662e-1` (18.47%) on the one allowed exception, `tier=parity` on rw/trend |

### Requirements Coverage

| Requirement | Source Plan | Status | Evidence |
|-------------|-------------|--------|----------|
| UPST-06 | 17-01 | ✓ SATISFIED | 0.16.1 consumed, no override, guards recorded and independently re-verified |
| DIAG-03 | 17-01 | ✓ SATISFIED | `regression` param + D-04 four-way mapping, verified live and via tests |
| DIAG-04 | 17-02 | ✓ SATISFIED | `fitted_params` df validation + R parity, verified live and via tests |
| DIAG-05 | 17-02 | ✓ SATISFIED | Docs rewritten, snippet harness 0 FAIL (re-run) |
| FCST-09 | 17-03 | ✓ SATISFIED | 36-row parity suite + MC coverage, re-run green |
| UPST-01 | 17-03 (remapped from Phase 11 in REQUIREMENTS.md, per D-15) | ✓ SATISFIED | `autoarima_selection_matches_r` test, 60/60 exact agreement |

REQUIREMENTS.md cross-check: all 6 IDs (UPST-06, DIAG-03, DIAG-04, DIAG-05, FCST-09, UPST-01) are mapped to "Phase 17" in `.planning/REQUIREMENTS.md`'s coverage table, and all 6 are claimed by the three plans' frontmatter `requirements:` fields. No orphaned requirements found for Phase 17.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| — | — | — | — | None found. `git grep` for TBD/FIXME/XXX across phase-touched files: 0 hits. The one code-review warning (WR-01, missing `SPECIAL_HANDLING` on `ts_ljung_box`/`ts_residual_diagnostics` optional-arg overloads) was fixed in commit `6ceea19` and independently re-verified live below. |

**WR-01 fix independently re-verified:**

```sql
SELECT (ts_ljung_box(x, NULL, 2)).lags  -- runs with default lags, not NULL row
```
Confirmed present in `test/sql/ts_diagnostics_reference.test` and covered by the 2309-assertion full-suite run below.

### Behavioral Spot-Checks (independently re-run by this verifier, not reused from SUMMARY claims)

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| No `[patch]`/override for anofox-forecast | `sed -n '/^\[patch/,/^\[/p' Cargo.toml \| grep -c anofox-forecast` | `0` | ✓ PASS |
| Cargo.lock single clean version | `cargo tree -i anofox-forecast` | single edge, v0.16.1 | ✓ PASS |
| No SSL/crypto linked into extension | `objdump -p .duckdb_extension \| grep NEEDED \| grep -i ssl\|crypto` | no output | ✓ PASS |
| Constant-series stationarity verdict | `SELECT (ts_stationarity(30×5.0)).verdict` | `constant` | ✓ PASS |
| Degenerate-series stationarity verdict | `SELECT (ts_stationarity([1,2,3])).verdict IS NULL` | `true` | ✓ PASS |
| Ljung-Box df<=0 is a named SQL error | `SELECT ts_ljung_box(sin-series, 5, 5)` | `Invalid Input Error: ...degrees of freedom lags - fitted_params must be >= 1` | ✓ PASS |
| ADF degenerate critical value is NaN | `SELECT ts_adf([1,2,3]).cv_5pct IS NULL OR isnan(...)` | `true` | ✓ PASS |
| `stationarity_reference` cargo test | `cargo test -p anofox-fcst-core --test stationarity_reference --release` | 4 passed | ✓ PASS |
| `portmanteau_reference` cargo test | `cargo test -p anofox-fcst-core --test portmanteau_reference --release` | 4 passed | ✓ PASS |
| `r_forecast_parity` cargo test | `cargo test -p anofox-fcst-core --test r_forecast_parity --release` | 3 passed | ✓ PASS |
| `interval_coverage_mc` cargo test | `cargo test -p anofox-fcst-core --test interval_coverage_mc --release` | 2 passed | ✓ PASS |
| `r_parity_diagnosis` ladder (full output) | `cargo test --test r_parity_diagnosis --release -- --nocapture` | 6 passed, 3 ignored; autoarima air `tier=outside` (0.1847), rw/trend `tier=parity` | ✓ PASS |
| `check_root_causes.py --stage final` | `python3 diagnosis/check_root_causes.py --stage final` | `invalid=0 uncovered=0 missing_drafts=0 repro_mismatch=0 orphans=0 uncaused=0 autoarima_gaps=0` | ✓ PASS |
| Phase 17 SQL reference tests | `test/sql/ts_diagnostics_reference.test`, `test/sql/ts_forecast_reference.test` | 72 + 19 assertions | ✓ PASS |
| Full SQL test suite | `./build/release/test/unittest "test/sql/*"` | All tests passed (50 skipped [require json], 2309 assertions, 35 cases) | ✓ PASS |
| `cargo fmt --all -- --check` | — | clean, exit 0 | ✓ PASS |
| Snippet harness (docs/skill/README) | `run_snippets.py --out <scratch>` | PASS 6, FAIL 0 | ✓ PASS |
| No stale Phase 11/17 tracking notes | `git grep -P 'Phase 11/17\|v0\.10\.0, Phase 1[17]\|known issue, Phase 11\|racked for Phase 11' docs plugins README.md examples` | 0 hits | ✓ PASS |

### Probe Execution

Not applicable — this phase has no `scripts/*/tests/probe-*.sh` convention; its own diagnosis/snippet-harness gates (`check_root_causes.py`, `run_snippets.py`) serve the equivalent role and are exercised above.

### Human Verification Required

None. Every must-have truth in this phase resolves to an observable, machine-checkable fact (numeric
tolerances against fixtures, SQL error messages, build/link artifacts) and was independently re-run by
this verifier against the live built extension and the Rust test binaries, not merely read from
SUMMARY.md.

### Gaps Summary

No gaps. All four ROADMAP success criteria are independently verified against the codebase and the built
extension:

1. anofox-forecast 0.16.1 consumed cleanly, no override, guards recorded and reproduced.
2. ADF/KPSS/stationarity corrected and regression-parameterized, D-04 four-way mapping verified live.
3. Ljung-Box `fitted_params` model-aware df validated live; diagnostics docs rewritten and snippet-harness
   clean on independent re-run.
4. 36-row R-forecast parity suite and Monte-Carlo coverage independently re-run green; the one documented
   known gap (AirPassengers AutoARIMA 95% interval, D-16) is itself reproduced by this verifier's own run
   of the diagnosis ladder with matching numbers, and is backed by a root-cause row, an upstream issue
   draft, and a `check_root_causes.py` integrity gate that explicitly forbids any *other* AutoARIMA check
   from carrying that exception.

One pre-existing code-review warning (WR-01) was found and fixed within this phase (commit `6ceea19`,
documented in 17-REVIEW-FIX.md) before this verification ran; its fix is independently confirmed above.
Two Info-level findings (IN-01, IN-02) remain open but are cosmetic/low-impact per the reviewer's own
assessment (empty-input edge case ordering; a degenerate-path `lags` field reporting `0` instead of `1`)
and do not affect any ROADMAP success criterion or requirement.

---

*Verified: 2026-10-15T20:30:00Z*
*Verifier: Claude (gsd-verifier)*
