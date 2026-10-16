---
phase: 13-core-statistics-metrics-data-quality
verified: 2026-10-10T23:40:25Z
status: passed
score: 5/5 must-haves verified
covered_files: [".planning/REQUIREMENTS.md", ".planning/phases/13-core-statistics-metrics-data-quality/13-01-PLAN.md", ".planning/phases/13-core-statistics-metrics-data-quality/13-01-SUMMARY.md", ".planning/phases/13-core-statistics-metrics-data-quality/13-02-PLAN.md", ".planning/phases/13-core-statistics-metrics-data-quality/13-02-SUMMARY.md", ".planning/phases/13-core-statistics-metrics-data-quality/13-03-PLAN.md", ".planning/phases/13-core-statistics-metrics-data-quality/13-03-SUMMARY.md", ".planning/phases/13-core-statistics-metrics-data-quality/13-04-PLAN.md", ".planning/phases/13-core-statistics-metrics-data-quality/13-04-SUMMARY.md", ".planning/phases/13-core-statistics-metrics-data-quality/13-CONTEXT.md", ".planning/phases/13-core-statistics-metrics-data-quality/13-REVIEW-FIX.md", ".planning/phases/13-core-statistics-metrics-data-quality/13-REVIEW.md", ".planning/phases/13-core-statistics-metrics-data-quality/deferred-items.md", "crates/anofox-fcst-core/src/calendar.rs", "crates/anofox-fcst-core/src/gaps.rs", "crates/anofox-fcst-core/src/metrics.rs", "crates/anofox-fcst-core/src/quality.rs", "crates/anofox-fcst-core/src/stats.rs", "crates/anofox-fcst-core/src/stl.rs", "crates/anofox-fcst-core/src/supsmu.rs", "crates/anofox-fcst-ffi/src/lib.rs", "crates/anofox-fcst-ffi/src/types.rs", "docs/api/03-statistics.md", "docs/api/04-data-preparation.md", "docs/api/09-evaluation-metrics.md", "docs/api/22-supported-frequencies.md", "src/aggregate_functions/ts_data_quality_agg.cpp", "src/aggregate_functions/ts_stats_agg.cpp", "src/include/anofox_fcst_ffi.h", "src/macros/ts_macros.cpp", "src/scalar_functions/metrics.cpp", "src/table_functions/ts_data_quality.cpp", "src/table_functions/ts_metrics_native.cpp", "src/table_functions/ts_stats.cpp"]
covered_digest: "v1:sha256:9a514793131fa48cbebb68488be2b5326858a5d31bbcff051f3e8b354b0f7e68"
behavior_unverified: 0
overrides_applied: 0
---

# Phase 13: Core Statistics, Metrics & Data Quality Verification Report

**Phase Goal:** `ts_stats`, the accuracy metrics and data-quality scoring return the textbook statistic
they are named after — scipy-exact moments, Wang/Hyndman STL strengths, Hyndman-Koehler MASE, an explicit
MAPE zero policy, and data-quality/frequency results that reflect the real dates.

**Verified:** 2026-10-10T23:40:25Z
**Status:** passed
**Re-verification:** No — initial verification

## Goal Achievement

All five ROADMAP success criteria were re-executed directly against the built extension
(`build/release/duckdb -unsigned` + `LOAD build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension`)
and against the Rust fixture test suites, independent of the SUMMARY.md narrative. The build
(`build/release/duckdb`, mtime 01:25) postdates the last source-affecting commit (`1d01594`,
`2026-10-11T01:23:54+02:00`); the only commit after the build (`8b05d13`) touches only
`.planning/` review docs, so the build is current — no rebuild was required.

### Observable Truths (ROADMAP Success Criteria)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | `ts_stats` skewness/kurtosis equal scipy `skew(bias=False)`/`kurtosis(bias=False)` to 1e-10 (x20 kurtosis +0.277, previously −0.096); bimodality coefficient uses the SAS small-sample formula (0.466, previously 0.600) | ✓ VERIFIED | Fresh ad-hoc SQL against the built extension on the literal x20/neg30 vectors from `validation/reference/fixtures/moments/scipy_moments.json`: `kurtosis = 0.2765286546322527` (fixture: `0.2765286546322532`, diff < 1e-10) and `bimodality_coef = 0.46612591998230263` (fixture: `0.4661259199823024`, diff < 1e-10) via `ts_stats_by`. `cargo test -p anofox-fcst-core --test stats_reference` passes (`moments_fixture_matches_compute_ts_stats`, all 7 vectors incl. neg30). |
| 2 | Trend/seasonal strength equal Wang/Hyndman STL F_T/F_S on R reference fixtures (random walk seasonal strength ≈0.05/0.09, previously 0.96); EDA skill thresholds updated; entropy/gap-count outputs correctly named/documented; `behavioral_score` no longer penalises a predictable smooth series without documented rationale | ✓ VERIFIED | `test/sql/ts_stats_reference.test` (62 assertions, **executed fresh**, all pass) includes the `rw150` block asserting `abs(seasonality_strength - 0.0865166431060249) < 1e-8` against `ts_stats_by`; fixture `r_stl_strength/cases.json`'s `audit_rw150` case independently confirms `tsfeatures::stl_features` gives `seasonal_strength = 0.0865166431060249` (not the old 0.96). `cargo test -p anofox-fcst-core --test stl_reference` passes (3 tests). Docs (`docs/api/03-statistics.md:92-93`) correctly distinguish `n_gaps` (missing periods) from `n_gap_runs` (gap occurrences). EDA skill (`SKILL.md:378`) states 0.3/0.6 thresholds. `behavioral_score` doc (`docs/api/03-statistics.md:179`) states the no-autocorrelation-penalty rationale explicitly; `grep -c acf_penalty quality.rs` confirms the old formula is gone. |
| 3 | `ts_mase` is Hyndman-Koehler matching R `forecast::accuracy()` MASE on fixtures; old relative-MAE result only as `ts_rmae`; MAPE with zero actuals follows one explicit policy | ✓ VERIFIED | `validation/reference/fixtures/r_mase/accuracy_mase.json`'s `air_snaive_m12` case: `mase_accuracy = mase_manual = 1.57088122605364` (matches ROADMAP's cited 1.570881). `cargo test -p anofox-fcst-core --test metrics_reference` passes (both tests, all 8 cases). `test/sql/ts_metrics_reference.test` (25 assertions, executed fresh, all pass) confirms the 3-list `ts_mase(actual,forecast,baseline)` form errors with a message containing `ts_rmae`, the 5-argument `ts_mase_by` baseline-column form errors naming `ts_rmae_by`, and `ts_mape([0.0,2.0,4.0],[1.0,2.0,5.0]) IS NULL` (zero-actual policy) while `ts_mape([0.0,2.0],[NULL,3.0]) = 50.0` (zero actual paired with NULL dropped first, not NULL-policy-triggered). |
| 4 | `ts_data_quality_by` on a series with known gaps reports the real gap count and temporal score < 1.0, honours frequency and n_short, deterministic across repeated runs on tied-mode data | ✓ VERIFIED | Fresh ad-hoc SQL: a 60-day daily series with days 20-39 removed gives `n_gaps=20, n_gap_runs=1, temporal_score=0.0` via `ts_data_quality_by`, identical on two consecutive runs (determinism). `test/sql/ts_data_quality_reference.test` (60 assertions, executed fresh, all pass) additionally confirms frequency-honouring (`'2d'` changes `n_gaps`), `n_short`-driven `is_short`/`structural_score`, and a tied-step determinism block (`dq_tie`, 3 repeated runs identical). |
| 5 | `detect_frequency` deterministic on tied step data with µs unit documented; monthly/quarterly/yearly gap fill on month-end data preserves month-end dates (2024-01-31 → 2024-02-29 → 2024-03-31) | ✓ VERIFIED | Fresh ad-hoc SQL: `ts_fill_gaps_by` on `(2024-01-31, 2024-04-30, 2024-05-31)` with `'1mo'` returns exactly `2024-01-31, 2024-02-29, 2024-03-31, 2024-04-30, 2024-05-31` — the literal sequence ROADMAP SC5 specifies. `grep -i microsecond` confirms the unit is documented at the Rust (`gaps.rs`), FFI (`anofox_fcst_ffi.h`/`lib.rs`), and SQL-doc (`docs/api/22-supported-frequencies.md:113`) levels. `test/sql/ts_fill_gaps_month_end.test` (53 assertions, executed fresh, all pass). `find_mode`/`count_gaps` (the old nondeterministic `HashMap`-tie-break source, per audit E7) are confirmed deleted from `quality.rs`. |

**Score:** 5/5 truths verified (0 present, behavior-unverified)

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `crates/anofox-fcst-core/src/gaps.rs` (`gap_counts`/`resolve_gap_step`) | shared missing-period gap counter reused by ts_stats + data quality | ✓ VERIFIED | Present, used by both `stats.rs` and `quality.rs`; `cargo test` fixture-driven assertions pass |
| `crates/anofox-fcst-core/src/stl.rs` (`stl`), `supsmu.rs` (`supsmu`) | R-faithful STL/supsmu ports driving F_T/F_S | ✓ VERIFIED | Present; `stl_reference.rs` component-level assertions (1e-8) pass against R fixtures |
| `crates/anofox-fcst-core/src/metrics.rs` (`mase`, `mape`) | HK MASE + MAPE NULL policy | ✓ VERIFIED | Present; `metrics_reference.rs` fixture assertions pass; wired through FFI/C++/macros (confirmed below) |
| `crates/anofox-fcst-core/src/quality.rs` (`compute_data_quality`) | date/frequency/n_short-aware quality scoring, new behavioral score | ✓ VERIFIED | Present; `quality_gaps_reference.rs` fixture assertions pass |
| `test/sql/ts_stats_reference.test`, `ts_data_quality_reference.test`, `ts_fill_gaps_month_end.test`, `ts_metrics_reference.test` | non-json-gated SQL regression suites | ✓ VERIFIED | All four executed fresh in this verification session (62+60+53+25 = 200 assertions), 0 failures |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `ts_macros.cpp` (`ts_stats_by`/`ts_stats`/`ts_data_quality*`) | `stats.rs`/`quality.rs` via FFI | `n_gap_runs`, `seasonal_period`, dates/frequency/n_short arguments | ✓ WIRED | Confirmed by direct SQL execution against the built extension (SC1, SC4, SC5 above) and by the passing non-gated SQL test suites |
| `metrics.cpp` (`ts_mase` overload set) | `anofox_ts_mase` FFI (insample + seasonal_period) | `out_is_null` NULL mapping | ✓ WIRED | Confirmed via the 3-list removal error and the 4-arg HK path both executing correctly in `ts_metrics_reference.test` |
| `ts_macros.cpp` (`ts_data_quality_by`) | `_ts_data_quality(values, dates, frequency, n_short)` | dates/frequency/n_short pass-through | ✓ WIRED | Confirmed by the SC4 fresh ad-hoc SQL run (real gap count, frequency sensitivity) |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|---|---|---|---|---|
| STAT-01 | 13-01 | scipy moments + SAS bimodality | ✓ SATISFIED | SC1 evidence above |
| STAT-02 | 13-02 | Wang/Hyndman STL F_T/F_S | ✓ SATISFIED | SC2 evidence above |
| STAT-03 | 13-01 | entropy/n_gaps semantics | ✓ SATISFIED | SC2 evidence above (entropy = Pincus ApEn; n_gaps/n_gap_runs distinguishable) |
| STAT-04 | 13-03 | behavioral_score redesign | ✓ SATISFIED | SC2 evidence above; `quality_gaps_reference.rs` fixture pass |
| STAT-05 | 13-04 | MAPE zero-actual policy | ✓ SATISFIED | SC3 evidence above |
| STAT-06 | 13-04 | Hyndman-Koehler MASE | ✓ SATISFIED | SC3 evidence above |
| WRAP-02 | 13-03 | ts_data_quality_by dates/frequency/n_short, deterministic | ✓ SATISFIED | SC4 evidence above |
| WRAP-03 | 13-03 | detect_frequency µs docs, month-end gap fill | ✓ SATISFIED | SC5 evidence above |

All 8 requirement IDs declared across the four plans' frontmatter (STAT-01..06, WRAP-02, WRAP-03) match
`.planning/REQUIREMENTS.md` exactly — no orphaned requirements found (REQUIREMENTS.md's Phase 13 mapping
lists exactly these 8 IDs; their table rows are still marked "Pending," a tracking-table bookkeeping lag,
not a functional gap — see Gaps Summary).

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| (none in Phase-13-modified files) | — | TBD/FIXME/XXX/TODO/HACK/PLACEHOLDER scan on all 16 Rust/C++ files this phase modified | — | Clean. The one `XXX` hit (`ts_stats.cpp:29`) is inside a code comment referencing DuckDB's `LogicalTypeId::XXX` enum member name, not a debt marker. |
| `src/macros/ts_macros.cpp:170-197` | `ts_data_quality_summary` | Known, disclosed defect (not introduced silently) | ℹ️ Info (disclosed, routed) | See Gaps Summary — WR-05, open, routed to Phase 18 at latest per the phase's own code-review loop |

### Behavioral Spot-Checks / Fresh Executed Evidence

| Behavior | Command | Result | Status |
|---|---|---|---|
| x20 kurtosis / neg30 bimodality via `ts_stats_by` | ad-hoc SQL against built extension | `0.2765286546322527` / `0.46612591998230263` (both within 1e-10 of fixture) | ✓ PASS |
| rw150 seasonal strength via `ts_stats_reference.test` | `./build/release/test/unittest test/sql/ts_stats_reference.test` | 62/62 assertions pass | ✓ PASS |
| AirPassengers HK MASE via r_mase fixture + `ts_metrics_reference.test` | `./build/release/test/unittest test/sql/ts_metrics_reference.test` | 25/25 assertions pass; `mase_accuracy == mase_manual == 1.57088...` | ✓ PASS |
| 60-day daily series, 20-day hole, `ts_data_quality_by` | ad-hoc SQL against built extension, run twice | `n_gaps=20, temporal_score=0.0`, identical on repeat | ✓ PASS |
| Month-end gap fill `2024-01-31→04-30→05-31` with `'1mo'` | ad-hoc SQL against built extension | returns `01-31, 02-29, 03-31, 04-30, 05-31` exactly | ✓ PASS |
| `cargo test --workspace` | full workspace run (once) | 1786 total assertions across all crates, 0 failed | ✓ PASS |
| `make test_release` | full SQL regression suite (once) | "All tests passed (50 skipped tests, 1824 assertions in 30 test cases)" | ✓ PASS |
| `examples/metrics/synthetic_metrics_examples.sql`, `examples/data_preparation/synthetic_data_prep_examples.sql` | `duckdb -unsigned -bail < ...` | both exit 0, "...EXAMPLES COMPLETE" | ✓ PASS |
| Phase-wide snippet harness (no file args = every markdown changed since BASE) | `python3 snippets/run_snippets.py --out /tmp/pass-list-verify.tsv` | `PASS: 12, EXPECTED-ERROR: 1, SYNOPSIS: 2, FAIL: 0` | ✓ PASS |
| `git grep -n "Phase 13" -- docs plugins README.md CHANGELOG.md examples` | stale-note grep | 0 hits | ✓ PASS |
| `cargo fmt --all -- --check` | fmt gate | clean | ✓ PASS |
| `cargo clippy` (pinned 1.89.0 toolchain) | clippy gate | 4 pre-existing failures, all at `decomposition.rs:70`, `features.rs:53`, `periods.rs:851/874` — none touched by this phase (confirmed via the review's own `git diff`); logged in `deferred-items.md` | ℹ️ Info (pre-existing, out of scope) |

### Human Verification Required

None. Every ROADMAP success criterion was independently re-executed with fresh, falsifiable evidence
(ad-hoc SQL against the built extension, plus full fixture-test and SQL-regression-suite runs) in this
verification session, not inferred from SUMMARY.md claims or symbol presence alone.

### Gaps Summary

No gaps block the phase goal. All five ROADMAP success criteria hold with fresh, directly-executed
evidence against the built extension, matching the specific numeric values ROADMAP itself cites (x20
kurtosis 0.277, bimodality 0.466, random-walk seasonal strength ≈0.09 under `tsfeatures`' `s.window=11`
— 13-02's `deferred-items.md` discloses that ROADMAP's "≈0.05" was the `s.window="periodic"` variant,
both measured on the same audit series, neither a discrepancy in the underlying fix — AirPassengers HK
MASE 1.570881, month-end gap fill sequence).

One disclosed, non-blocking issue is carried forward from this phase's own code-review loop and is
**not** a failure of any stated ROADMAP success criterion (none of the five criteria mention
`ts_data_quality_summary`'s bucket-total consistency):

- **WR-05 (open, routed):** `ts_data_quality_summary` undercounts groups whose `date_col` is NULL for
  every row of the group — the group's quality struct becomes NULL (via a `FILTER (WHERE date_col IS
  NOT NULL)` added by this phase's own CR-02 fix, which correctly handles *partial* NULL dates), and
  DuckDB's `SUM(CASE WHEN ... score >= x THEN 1 END)`/`AVG` bucketing then silently drops that group
  from `n_good`/`n_fair`/`n_poor`/`avg_score` while still counting it in `n_total`. Reproduced fresh in
  this verification session (`mixed2` table, exact review repro): `n_total=2, n_good=1, n_fair=0,
  n_poor=0, avg_score=1.0` — the all-NULL-dated group vanishes from every per-bucket count. This is
  explicitly disclosed in `13-REVIEW.md`/`13-REVIEW-FIX.md` (iteration-3 Warning, not Critical; the
  3-iteration review-fix cap was reached) and in `deferred-items.md`, with an explicit routing target
  ("before milestone close (Phase 18 regression suite at the latest)"). Recommend the orchestrator
  confirm this routing is still acceptable before milestone close, but it does not block Phase 13's own
  goal: `ts_data_quality_by`/`ts_data_quality` (the forms ROADMAP SC4 actually names) are unaffected.

Separately, `.planning/REQUIREMENTS.md`'s tracking table still marks all 8 of this phase's requirement
IDs (STAT-01..06, WRAP-02, WRAP-03) as "Pending" even though every one is satisfied with executed
evidence above — this is a bookkeeping lag in the requirements-tracking table, not a functional gap;
flagging for the orchestrator to update at milestone close.

---

_Verified: 2026-10-10T23:40:25Z_
_Verifier: Claude (gsd-verifier)_
