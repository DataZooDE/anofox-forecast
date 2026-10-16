---
phase: 18-correctness-regression-suite-audit-docs
verified: 2026-10-16T00:00:00Z
status: passed
score: 5/5 must-haves verified
covered_files: [".github/workflows/audit.yml", ".planning/REQUIREMENTS.md", ".planning/phases/18-correctness-regression-suite-audit-docs/18-01-PLAN.md", ".planning/phases/18-correctness-regression-suite-audit-docs/18-01-SUMMARY.md", ".planning/phases/18-correctness-regression-suite-audit-docs/18-02-PLAN.md", ".planning/phases/18-correctness-regression-suite-audit-docs/18-02-SUMMARY.md", ".planning/phases/18-correctness-regression-suite-audit-docs/18-03-PLAN.md", ".planning/phases/18-correctness-regression-suite-audit-docs/18-03-SUMMARY.md", ".planning/phases/18-correctness-regression-suite-audit-docs/18-04-PLAN.md", ".planning/phases/18-correctness-regression-suite-audit-docs/18-04-SUMMARY.md", ".planning/phases/18-correctness-regression-suite-audit-docs/18-05-PLAN.md", ".planning/phases/18-correctness-regression-suite-audit-docs/18-05-SUMMARY.md", ".planning/phases/18-correctness-regression-suite-audit-docs/18-CONTEXT.md", ".planning/phases/18-correctness-regression-suite-audit-docs/18-PATTERNS.md", ".planning/phases/18-correctness-regression-suite-audit-docs/18-RESEARCH.md", ".planning/phases/18-correctness-regression-suite-audit-docs/18-REVIEW-FIX.md", ".planning/phases/18-correctness-regression-suite-audit-docs/18-REVIEW.md", ".planning/phases/18-correctness-regression-suite-audit-docs/deferred-items.md", "CHANGELOG.md", "Cargo.lock", "Cargo.toml", "Makefile", "docs/release-notes/v0.10.0.md", "extension_config.cmake", "scripts/audit_allowlist.txt", "scripts/audit_no_stats_in_cpp.py", "scripts/check_correctness_inventory.py", "scripts/check_doc_snippets.py", "scripts/check_length_only_tests.py", "scripts/length_only_allowlist.txt", "src/include/ts_forecast_params.hpp", "src/macros/ts_macros.cpp", "src/table_functions/ts_cv_split_native.cpp", "src/table_functions/ts_fill_forward_native.cpp", "src/table_functions/ts_fill_gaps_native.cpp"]
covered_digest: "v1:sha256:740a5d27afebd1cf2014d681a5e5deae8daa7c857e2074e9a08ae2f10c0c66df"
behavior_unverified: 0
overrides_applied: 0
---

# Phase 18: Correctness Regression Suite, Audit & Docs — Verification Report

**Phase Goal:** The corrected behaviour is pinned against regression, guarded against statistics creeping back into
C++/SQL, and accurately described in docs, skills and release notes.
**Verified:** 2026-10-16
**Status:** passed
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

| # | Truth (ROADMAP success criterion) | Status | Evidence |
|---|---|---|---|
| 1 | Every test in modules touched this milestone asserts fixture/closed-form values; sweep finds 0 length-only assertions | ✓ VERIFIED | `python3 scripts/check_length_only_tests.py` → `Summary: 0 un-allow-listed length-only tests.` exit 0 (re-ran independently). `--self-test` 6/6 PASS, exit 0. `--check-module-list` confirms the frozen `MILESTONE_MODULES` list still matches `git diff --name-only b1261fe..HEAD -- crates` exactly (no drift), exit 0. |
| 2 | `test/sql/correctness/*.test` pins the corrected result of every user-facing function fixed this milestone, passes against the built extension | ✓ VERIFIED | 9 files present (`stats`, `metrics`, `data_quality`, `data_prep`, `features`, `detection_decomposition`, `conformal_bootstrap`, `diagnostics`, `forecast_parity`); none carries `^require json`. Independently ran `./build/release/test/unittest "test/sql/correctness/*"` → `All tests passed (331 assertions in 9 test cases)`. Full suite `./build/release/test/unittest "test/sql/*"` → `All tests passed (5580 assertions in 94 test cases)`, matching the orchestrator's stated baseline exactly. `python3 scripts/check_correctness_inventory.py` → exit 0, `116 CHANGELOG-mentioned registered functions, 0 missing correctness coverage, 2 not registered` (the 2 are intentionally-removed `ts_backtest_auto_by`/`_ts_backtest_native`, informational). |
| 3 | An audit script (run in CI or as a test) fails when statistical arithmetic appears in `src/**/*.cpp`/macro SQL outside an allow-list, and passes on HEAD | ✓ VERIFIED | `python3 scripts/audit_no_stats_in_cpp.py --self-test` → 12 positive/negative category checks + 4 allow-list checks passed, exit 0. Real scan → `audit: 34 hits, 34 allow-listed, 0 violations, 0 stale entries`, exit 0. **Independently re-ran the injection probe**: wrote `src/zz_audit_probe.cpp` with a `std::sqrt` variance line and a raw-string `stddev(x)` SQL query → audit exits 1, naming both injected lines (`probe lines named: 2`); deleted the probe and re-ran → exit 0 again, `git status --porcelain -- src` clean. `.github/workflows/audit.yml` exists, triggers on `push`/`pull_request`/`workflow_dispatch`, `permissions: contents: read` only, no `pull_request_target`/secrets, `actions/checkout@v4` with `persist-credentials: false`, runs the audit self-test + real audit + the D-03 sweep as three direct `python3` steps — satisfies "run in CI" (orchestrator/18-REVIEW WR-04 disposition: `check_correctness_inventory.py`/`check_doc_snippets.py` need a full build so they're documented release-DoD steps, not CI jobs — that disposition applies to those two gates, not to this one, which IS wired into CI). |
| 4 | `docs/` and `plugins/anofox-forecast/skills/` (bodies + frontmatter) describe the corrected definitions, every changed SQL snippet executed against the built extension | ✓ VERIFIED | Independently ran `make check-docs` → `check_doc_snippets: 343 PASS, 0 FAIL, 8 EXPECTED-ERROR, 154 SYNOPSIS, 4 SKIP-NETWORK, 0 STALE`, exit 0. Spot-checked content: `n_unassessed` documented in `docs/api/03-statistics.md:313-314` and `plugins/anofox-forecast/skills/anofox-forecast-eda/SKILL.md:141-143` with the four-bucket invariant stated. AutoARIMA/RC-05 resolution correctly narrated in past tense in `docs/api/07-forecasting.md:354-365` ("the one former interval exception is resolved... anofox-forecast 0.16.2 fixed it upstream; the interval now re-measures at 4.592775e-3..."), matching the live re-measured value in CHANGELOG.md and 18-05-SUMMARY.md. `grep -rn "RC-05\|known.gap"` across docs/plugins/CHANGELOG shows only legitimate, still-open gaps (RC-02/RC-03/RC-04 for Holt/HoltWinters/AutoETS/SESOptimized — none of which this milestone claims to fix) plus RC-05 consistently described as resolved. 0 stale in-progress-milestone-note hits (`grep -rnE 'fix planned for v0\.10\.0\|tracked for Phase [0-9]\|being corrected in v0\.10\.0\|planned for v0\.10\.0'` → 0). |
| 5 | CHANGELOG / release notes contain a breaking-change section listing every behaviour change that alters numerical output | ✓ VERIFIED | `CHANGELOG.md` `## [Unreleased] — v0.10.0 Statistical Correctness` opens with `### Breaking changes in v0.10.0` (line 13) containing a "Numerical output changes by area" table (stats/metrics/data quality/features/detection/conformal/diagnostics/forecasts, each row citing a phase, including the Phase 18 WR-05/D-10 and 18-05 AutoARIMA-interval rows) and a "SQL signature and semantics changes" table, followed by the pre-existing per-phase `### Breaking changes — details by phase` bullets (line 71, unchanged in count). `docs/release-notes/v0.10.0.md` exists (8920 bytes), names `anofox-forecast 0.16.2`, `anofox-regression 0.5.15`, `fdars-core 0.47`, `MSRV 1.89`, and passes the snippet checker (0 FAIL among its blocks, part of the `make check-docs` 0-FAIL/0-STALE total above). |

**Score:** 5/5 truths verified (0 present, behavior-unverified)

### Required Artifacts

| Artifact | Expected | Status | Details |
|---|---|---|---|
| `scripts/check_length_only_tests.py` + `scripts/length_only_allowlist.txt` | D-03 sweep, self-test, frozen module list | ✓ VERIFIED | Present, re-run clean (exit 0 on all three modes) |
| `test/sql/correctness/*.test` (9 files) | TEST-03 fixture/closed-form pins | ✓ VERIFIED | All present, 0 json-gated, 331 assertions, all pass |
| `scripts/check_correctness_inventory.py` | CHANGELOG-vs-coverage gate | ✓ VERIFIED | Re-run: exit 0, 0 missing |
| `scripts/audit_no_stats_in_cpp.py` + `scripts/audit_allowlist.txt` | RUST-10 grep-based audit | ✓ VERIFIED | Re-run: 0 violations/0 stale; probe-injection re-proven live (see Truth 3) |
| `.github/workflows/audit.yml` | CI home for the audit + length-only sweep | ✓ VERIFIED | Parses correctly; least-privilege; runs both scripts; `MainDistributionPipeline.yml` untouched (`git diff --quiet` exit 0, re-confirmed) |
| `Makefile` `audit` / `check-docs` targets | Local entry points | ✓ VERIFIED | Both present and exit 0 when re-run |
| `scripts/check_doc_snippets.py` + `scripts/doc_snippets/*` | D-06 permanent doc/skill checker | ✓ VERIFIED | Re-run: 343 PASS/0 FAIL/0 STALE |
| `CHANGELOG.md` | Breaking-changes consolidation | ✓ VERIFIED | Present, structured as required |
| `docs/release-notes/v0.10.0.md` | Release notes | ✓ VERIFIED | Present, passes checker, names dependency versions |

### Key Link Verification

| From | To | Via | Status | Details |
|---|---|---|---|---|
| `.github/workflows/audit.yml` | `scripts/audit_no_stats_in_cpp.py` + `scripts/check_length_only_tests.py` | direct `python3` run steps | WIRED | Confirmed by reading the workflow file; both scripts invoked |
| `Makefile audit` | `scripts/audit_no_stats_in_cpp.py` (self-test + real) + `scripts/check_length_only_tests.py` | `make audit` target | WIRED | Re-ran `make audit`, all three sub-commands execute in order, exit 0 |
| `Makefile check-docs` | `scripts/check_doc_snippets.py` | `make check-docs` target | WIRED | Re-ran, exit 0 |
| `scripts/check_correctness_inventory.py` | `CHANGELOG.md` Unreleased section + `duckdb_functions()` | backticked-identifier intersection | WIRED | Re-ran against the live build; 116 functions matched |
| `test/sql/correctness/*.test` | `validation/reference/fixtures/<family>/*.json` | python one-liners cited in file headers | WIRED | Spot-checked `data_quality.test`/`metrics.test` headers (18-02-SUMMARY D08 matrix); code review (18-REVIEW.md) independently hand-verified several closed forms (sMAPE/R2/MASE/date arithmetic) |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|---|---|---|---|
| Length-only sweep detects real findings and allow-list stale entries | `check_length_only_tests.py --self-test` | 6/6 synthetic cases pass | ✓ PASS |
| Length-only sweep frozen module list matches git history | `check_length_only_tests.py --check-module-list` | "MILESTONE_MODULES matches `git diff --name-only` exactly" | ✓ PASS |
| Audit script detects injected statistical arithmetic (C++ + SQL) | inject `src/zz_audit_probe.cpp` (std::sqrt + raw-string stddev), run audit, delete probe, re-run | exit 1 naming both lines, then exit 0 clean, `git status` clean | ✓ PASS |
| Audit script self-test covers all categories + allow-list behaviors | `audit_no_stats_in_cpp.py --self-test` | "12 positive/negative category checks + 4 allow-list checks passed" | ✓ PASS |
| Full SQL regression suite green | `./build/release/test/unittest "test/sql/*"` | All tests passed (5580 assertions in 94 test cases) | ✓ PASS |
| Correctness-only suite green | `./build/release/test/unittest "test/sql/correctness/*"` | All tests passed (331 assertions in 9 test cases) | ✓ PASS |
| Doc/skill snippet checker | `make check-docs` | 343 PASS, 0 FAIL, 0 STALE | ✓ PASS |
| Correctness inventory gate | `python3 scripts/check_correctness_inventory.py` | exit 0, 0 missing | ✓ PASS |
| Rust workspace tests | `cargo test --workspace --release` | all test binaries "ok", no FAILED/error lines | ✓ PASS |
| Rust lint gates | `cargo fmt --all -- --check`, `cargo clippy --all-targets --all-features -- -D warnings` | both clean | ✓ PASS |
| Extension has no OpenSSL/crypto runtime dependency | `objdump -p .../anofox_forecast.duckdb_extension \| grep NEEDED` | libgcc_s, libm, libstdc++, libc, ld-linux only | ✓ PASS |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|---|---|---|---|---|
| TEST-02 | 18-02 | Length-only tests upgraded to value assertions | ✓ SATISFIED | Sweep exits 0 (re-verified live); 24 of 39 initial findings upgraded with derivation-cited value assertions, 15 allow-listed with justification, reviewed in 18-REVIEW.md (WR-03 tightened 6 overly-wide bounds, fixed per 18-REVIEW-FIX.md) |
| TEST-03 | 18-01/18-02 | Correctness suite pins corrected results | ✓ SATISFIED | 9-file suite + inventory gate, both re-verified live |
| RUST-10 | 18-03 | Audit confirms no statistics in C++/SQL beyond marshalling allow-list | ✓ SATISFIED | Audit + probe-injection re-verified live; CI wiring confirmed by reading `.github/workflows/audit.yml` |
| DOCS-01 | 18-04/18-05 | docs/skills describe corrected definitions, snippets run against built extension | ✓ SATISFIED | `make check-docs` re-verified live; content spot-checks for n_unassessed, AutoARIMA/RC-05 resolution, MASE, strengths |
| DOCS-02 | 18-04/18-05 | CHANGELOG/release notes list all numerical-output behaviour changes | ✓ SATISFIED | CHANGELOG structure + release notes verified directly |
| FCST-09 | 17 (reaffirmed 18-05) | R forecast parity suite | ✓ SATISFIED | 18-05 gap closure: RC-05 flipped to `parity`, `repro_rc_05` inverted into a standing regression guard; re-measured values cited consistently across fixtures/docs/CHANGELOG |
| UPST-06 | 17 (reaffirmed 18-05) | Upstream changes consumed as a released crate version | ✓ SATISFIED | `Cargo.toml` pins `anofox-forecast = { version = "0.16.2" }`, no `[patch]`/git/path override (confirmed by reading `Cargo.toml` directly) |

**Orphaned requirements check:** `.planning/REQUIREMENTS.md` maps exactly TEST-02, TEST-03, RUST-10, DOCS-01, DOCS-02 to "Phase 18" — all five are claimed by plan frontmatter across 18-01..18-04 (`requirements: [TEST-03]`, `[TEST-02, TEST-03]`, `[RUST-10]`, `[DOCS-01, DOCS-02]`). FCST-09/UPST-06 are mapped to Phase 17 in REQUIREMENTS.md but are explicitly reaffirmed/consumed by the gap-closure plan 18-05 per the orchestrator's framing and 18-05's own frontmatter (`requirements: [FCST-09, UPST-06, DOCS-01, DOCS-02]`) — not orphaned, correctly in scope for this gap-closure wave. No orphaned requirements found.

Note: `.planning/REQUIREMENTS.md`'s checkbox list (lines 16-17, 55, 111-112) and its traceability table (lines 132-133, 159, 194-195) still show TEST-02/TEST-03/RUST-10/DOCS-01/DOCS-02 as unchecked/"Pending" — this is stale tracking metadata (REQUIREMENTS.md is explicitly off-limits for plan-level edits per every plan's "never edit .planning/REQUIREMENTS.md" instruction) that the orchestrator updates post-verification; it does not reflect a gap in the actual implementation, which is complete and verified above.

### Anti-Patterns Found

No debt markers (`TBD`/`FIXME`/`XXX`) found in any phase-touched file (`scripts/audit_no_stats_in_cpp.py`, `scripts/check_length_only_tests.py`, `scripts/check_doc_snippets.py`, `scripts/check_correctness_inventory.py`, `.github/workflows/audit.yml`, `test/sql/correctness/*.test`). No stub patterns, placeholder returns, or weakened assertions found in the reviewed scripts or test files.

The 18-REVIEW.md code review (standard depth, 21 files + spot-checked `.test` files) found 0 critical issues and 5 warnings, all fixed in 18-REVIEW-FIX.md (re-verified live in this report: the `std::stoll` try/catch and the `ts_fill_gaps_native.cpp` NULL-frequency guard are both present in the current source; `cargo fmt`/`cargo test --workspace --release` both clean after the WR-03 bound-tightening). 2 INFO-tier items (negative-embargo silent no-op, hand-maintained module list) were left open as low-priority, non-blocking notes — consistent with their INFO classification.

### Deferred Items

Logged in `deferred-items.md`, correctly out of scope for this phase's must-haves (no success criterion requires them):
- Bare `frequency::VARCHAR` gaps in `ts_forecast_panel_by`/`ts_forecast_var_by`/`ts_stats(_by)` and sibling `'window'`/`'seasonal_periods'` validation gaps (18-01) — not exercised by any json-gated test, out of RUST-10/TEST-03 scope.
- `examples/metrics/README.md` and `examples/backtesting/m5_backtest_examples.sql` still use the deprecated metric `_by` table-name form (18-02/18-04) — `examples/**` is outside `check_doc_snippets.py`'s scan directories (`README.md`, `docs/**/*.md`, `plugins/anofox-forecast/skills/**/*.md`), so this does not violate DOCS-01 as scoped by the must-haves.
- `ts_features_config_from_json`/`_csv`'s `"features"` allow-list doesn't narrow output (18-04) — documented as observed behavior in the EDA skill rather than claimed-working; out of scope for a docs-only plan with no C++/Rust file in its remit.

None of these affect the roadmap success criteria as written.

### Human Verification Required

None. Every must-have was independently re-executed against the live codebase (not read from SUMMARY claims alone): the length-only sweep, the audit script (including a live fail/pass injection-probe round-trip), the correctness suite, the inventory gate, the doc-snippet checker, the full SQL regression suite, `cargo fmt`/`clippy`/`test --workspace --release`, and the `objdump` NEEDED check. All are deterministic, machine-checkable gates with no UI/UX/visual/real-time component requiring human judgment.

### Gaps Summary

No gaps found. All 5 ROADMAP success criteria are independently verified against the live codebase: the length-only sweep exits 0 with a self-proving test harness; the correctness suite (9 files, 331 assertions) passes and is backed by a 0-missing inventory gate; the no-statistics audit genuinely fails on injected arithmetic and passes cleanly on HEAD, wired into a least-privilege CI workflow; docs/skills describe the corrected definitions with 0 FAIL/0 STALE on the snippet checker; and CHANGELOG/release notes carry a structured, phase-cited breaking-changes section. The gap-closure plan 18-05 (anofox-forecast 0.16.2, RC-05 resolution) is fully consistent across code, fixtures, docs, and CHANGELOG, and the full release DoD (fmt/clippy/test/build/SQL-suite/audit/check-docs/inventory/objdump) reproduces green exactly as claimed.

---

_Verified: 2026-10-16_
_Verifier: Claude (gsd-verifier)_


## Addendum (2026-10-16): gap plan 18-06, milestone-audit gap DET-01

The v0.10.0 milestone audit found `ts_detect_periods` / `ts_detect_periods_by` returning an all-NULL row in the default call. Gap plan 18-06 fixed it (ed3c390: SPECIAL_HANDLING on the `_ts_detect_periods` expected_periods overloads; 281560c: the macro's `expected_periods` used `json_extract` where it needed `json_extract_string`; plus correctness pins; d0ea55a: retired the docs workarounds). The orchestrator re-verified at 1f57487:
- 6-arg `_ts_detect_periods(..., NULL::DOUBLE[], ...)` gives primary_period 12.0 on a period-12 sinusoid; `ts_detect_periods_by(..., MAP{})` gives n_periods 1, primary_period 12.0, method fft; the `expected_periods` '[12.0]' path gives matches_expected true; a NULL series gives NULL.
- `build/release/test/unittest "test/sql/*"`: All tests passed (5593 assertions in 94 test cases).
- `make check-docs`: 344 PASS, 0 FAIL, 0 STALE.
Status remains passed.
