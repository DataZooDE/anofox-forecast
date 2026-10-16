---
phase: 10-forecast-wrapper-delegates-to-crate-models
plan: 09
subsystem: docs
tags: [docs, reference-pages, tbats, intermittent, panel, laplace, garch, ensemble, snippet-harness]

requires:
  - phase: 10-03
    provides: "ensemble crate intervals (confidence_level, default 0.90), ['ETS','ETS'] = standalone ETS identity, strict case-insensitive panel croston_variant/model_pool parsing, requires_seasonal_period auto-detect-or-error rule"
  - phase: 10-06
    provides: "docs/api/07-forecasting.md's 'Prediction intervals, fitted values and input handling' section (link target for these 10 pages); the phase-local snippet harness (run_snippets.py, fixtures.sql, synopsis.txt)"
  - phase: 10-07
    provides: "per-model reference-page wording precedent (crate-interval paragraph, intermittent-demand interval-calibration caveat) reused verbatim for IMAPA/TSB"
  - phase: 10-08
    provides: "TBATS page's crate-interval + too-narrow-known-issue wording, reused verbatim for AutoTBATS; the ts_detect_periods_agg workaround pattern for the known ts_detect_periods_by NULL-row bug"
provides:
  - "10 per-model reference pages (docs/reference/models/multi-seasonal/auto_tbats.md, intermittent/{imapa,tsb,global_croston}.md, exponential-smoothing/global_ets.md, distributional/laplace.md, classical/garch.md, ensemble/{autoensemble,ensemble_explicit,ensemble_inspect}.md) rewritten for Phase 10 behaviour (D-01, D-05, D-07, D-13), every quick-task-261009-d02 correction note removed from these pages"
  - "global_croston.md/global_ets.md document croston_variant/model_pool as case-insensitive and strict ('classic'|'sba' / 'reduced'|'complete'); any other value is InvalidInput/InvalidParameter naming the bad value — proven with executed error examples"
  - "laplace.md documents Laplace's own distributional interval (level clamped to [0.5, 0.999]) passed through unchanged, no longer overwritten by a generic heuristic"
  - "garch.md documents D-01 passthrough intervals with an explicit non-bracketing caveat (yhat = conditional standard deviation; bounds center a statsforecast-style band on a different, simulated point forecast; ~0.13x reference width) and a conformal-intervals pointer, proven with an executed brackets_yhat=false example"
  - "autoensemble.md/ensemble_explicit.md document crate Ensemble/AutoEnsemble intervals at confidence_level (default 0.90, no longer NULL); ensemble_explicit.md adds an executed confidence_level:=0.95 example and an executed ['ETS','ETS']==standalone-ETS identity example"
  - "ensemble_explicit.md/ensemble_inspect.md's seasonal_period documentation corrected after direct testing against the built extension: 0 and 1 behave identically (both auto-detect; there is no separate explicit-1-forces-non-seasonal override at this surface) — only seasonal_period > 1 is honoured literally; seasonal-required members now error naming the model instead of the stale 'falls back to period=12' claim"
  - "Phase-wide sweep: every remaining '(being corrected in v0.10.0, Phase N)' placeholder note across docs/ and plugins/ (53 occurrences, 13 files outside this plan's own scope) reworded to '(tracked for Phase N)', preserving every substantive caveat"
  - "pass-list-10-09{,b}.tsv — per-block execution records for all 10 pages, 0 FAIL each"
affects: ["11", "13", "14", "15", "16", "17"]

actuals:
  tokens: 20719
  tasks: 2
  commits: 3
  plan_head_before: 12a5ce0c95b604b626149296672fa4553367a3c7

tech-stack:
  added: []
  patterns:
    - "Reused the 10-06 phase-local snippet harness unmodified — no changes to run_snippets.py/fixtures.sql, only additive synopsis.txt entries and new pass-list-10-09*.tsv outputs"
    - "Before documenting a nuanced claim from the plan's own <interfaces> block (e.g. 'explicit seasonal_period=1 = non-seasonal'), tested it directly against the built extension rather than transcribing the claim verbatim — found the actual forecast_explicit_ensemble/inspect_explicit_ensemble code path treats seasonal_period 0 and 1 identically (both auto-detect; only >1 wins), contradicting the interfaces note and a stale Rust doc-comment in the same function. Documented the tested behaviour, not the assumed one, per this plan's own 'describe only behaviour the built extension actually shows' constraint."
    - "A phase-wide placeholder-note sweep (grep -rl across docs/ and plugins/) is a mechanical find-and-reword operation (literal phrase retirement), not a content deletion — each of the 53 occurrences outside this plan's own file list named a real, still-open issue tracked for a specific future phase (11-17); the fix preserves the caveat's substance and only removes the exact grep-matched marker string the plan's acceptance criterion checks for."

key-files:
  modified:
    - docs/reference/models/multi-seasonal/auto_tbats.md
    - docs/reference/models/intermittent/imapa.md
    - docs/reference/models/intermittent/tsb.md
    - docs/reference/models/intermittent/global_croston.md
    - docs/reference/models/exponential-smoothing/global_ets.md
    - docs/reference/models/distributional/laplace.md
    - docs/reference/models/classical/garch.md
    - docs/reference/models/ensemble/autoensemble.md
    - docs/reference/models/ensemble/ensemble_explicit.md
    - docs/reference/models/ensemble/ensemble_inspect.md
    - docs/api/02-hierarchical.md
    - docs/api/03-statistics.md
    - docs/api/04-data-preparation.md
    - docs/api/05a-decomposition.md
    - docs/api/09-evaluation-metrics.md
    - docs/api/10-diagnostics.md
    - docs/api/11-conformal-prediction.md
    - docs/api/20-feature-extraction.md
    - docs/api/21-feature-reference.md
    - plugins/anofox-forecast/skills/anofox-forecast-backtest/SKILL.md
    - plugins/anofox-forecast/skills/anofox-forecast-data-prep/SKILL.md
    - plugins/anofox-forecast/skills/anofox-forecast-detection/SKILL.md
    - plugins/anofox-forecast/skills/anofox-forecast-eda/SKILL.md
    - .planning/phases/10-forecast-wrapper-delegates-to-crate-models/snippets/synopsis.txt
  created:
    - .planning/phases/10-forecast-wrapper-delegates-to-crate-models/snippets/pass-list-10-09.tsv
    - .planning/phases/10-forecast-wrapper-delegates-to-crate-models/snippets/pass-list-10-09b.tsv

key-decisions:
  - "Documented ensemble_explicit.md/ensemble_inspect.md's seasonal_period semantics as tested (0 and 1 both auto-detect; only >1 wins literally) rather than as the plan's <interfaces> note claimed ('explicit 1 = non-seasonal') — verified by direct comparison: seasonal_period:=0, :=1, and omitted all produced identical results on the same series/members, while :=2 and :=7 produced different results, and a HoltWinters member with seasonal_period:=1 on a non-seasonal series raised the same 'requires a seasonal period > 1' error as seasonal_period:=0 (not a silent non-seasonal fit)."
  - "Phase-wide '(being corrected in v0.10.0, Phase N)' sweep applied mechanically via a Python regex pass across all 13 files outside this plan's own scope, rewording to '(tracked for Phase N)' rather than deleting the caveats — the plan's acceptance criterion checks for the literal phrase's absence repo-wide, and the underlying issues (statistics moment-mixing, diagnostics ADF augmentation, feature-extraction PACF lag bug, etc.) are real and still open for their respective future phases; deleting the caveats would have hidden known limitations, not fixed them."
  - "GlobalCroston's/GlobalETS's error examples and messages were captured by running the actual misspelled-value calls against the built extension rather than guessing the wrapper text, after discovering the FFI message format ('ts_forecast_panel_by (method=...): unknown croston_variant ... ; expected ...') differs from the crate-internal PanelForecastError string alone."

patterns-established:
  - "When a plan's <interfaces>/<context> block states a specific parameter-resolution rule, verify it with a direct comparison query against the built extension (same series/members, varying only the parameter under test) before writing it into the reference page — a stale doc comment in the Rust source or a planning-time assumption can both be wrong, and this plan's hard constraint ('describe only behaviour the built extension actually shows') takes precedence over the plan's own prose when the two conflict."

requirements-completed: [FCST-01, FCST-06, FCST-07]

coverage:
  - id: D1
    description: "auto_tbats.md, imapa.md and tsb.md describe crate intervals (AutoTBATS carries the too-narrow known issue and the period auto-detect-or-error rule); global_croston.md documents croston_variant 'classic'|'sba' (case-insensitive, default classic, any other value an error); global_ets.md documents model_pool 'reduced'|'complete' (case-insensitive, default reduced, any other value an error)"
    requirement: FCST-06
    verification:
      - kind: e2e
        ref: "python3 run_snippets.py --all --out pass-list-10-09.tsv on the 5 Task 1 pages: 7 PASS, 8 SYNOPSIS, 0 FAIL, exit 0; misspelled croston_variant/model_pool examples executed against the built extension (Invalid Input Error naming the bad value)"
        status: pass
      - kind: unit
        ref: "grep -l 'being corrected in v0.10.0' on auto_tbats.md/imapa.md/tsb.md: empty; grep -ci error global_croston.md >= 1; grep -ci case-insensitive global_ets.md >= 1"
        status: pass
    human_judgment: false
  - id: D2
    description: "laplace.md states Laplace's distributional intervals are returned unchanged (level clamped to [0.5, 0.999]); garch.md states bounds are now returned as a statsforecast-style band around GARCH's simulated innovation path while yhat is the conditional standard deviation (band does not bracket yhat), and points to conformal intervals"
    requirement: FCST-01
    verification:
      - kind: e2e
        ref: "python3 run_snippets.py --all --out pass-list-10-09b.tsv on laplace.md/garch.md: 0 FAIL; garch.md's brackets_yhat example executed against the built extension (non-NULL bounds, at least one row with brackets_yhat=false)"
        status: pass
      - kind: unit
        ref: "grep -c 'conditional standard deviation' garch.md >= 1"
        status: pass
    human_judgment: false
  - id: D3
    description: "autoensemble.md and ensemble_explicit.md document crate Ensemble/AutoEnsemble intervals (confidence_level, default 0.90) and the ['ETS','ETS']==ETS identity; ensemble_explicit.md and ensemble_inspect.md document the tested seasonal_period 0/1 auto-detect behaviour (corrected from the plan's assumed '1=non-seasonal')"
    requirement: FCST-07
    verification:
      - kind: e2e
        ref: "python3 run_snippets.py --all --out pass-list-10-09b.tsv on autoensemble.md/ensemble_explicit.md/ensemble_inspect.md: 0 FAIL; ets_identity_demo mismatched_steps=0 executed against the built extension; confidence_level:=0.95 example executed"
        status: pass
      - kind: unit
        ref: "grep -c confidence_level ensemble_explicit.md >= 2; grep -ciE 'deferred to (Phase 6|EPI-01)' autoensemble.md ensemble_explicit.md == 0 per file"
        status: pass
    human_judgment: false
  - id: D4
    description: "No page in this plan carries the Phase-10 correction note and every SQL block runs against the built extension; no '(being corrected in v0.10.0, ...)' marker remains anywhere in docs/, plugins/ or README.md"
    requirement: FCST-01
    verification:
      - kind: e2e
        ref: "grep -rl 'being corrected in v0.10.0' docs/ plugins/ README.md: empty (0); two run_snippets.py --all invocations covering all 10 pages: 0 FAIL, exit 0 each"
        status: pass
    human_judgment: false

duration: ~1h (single session)
completed: 2026-10-09
status: complete
---

# Phase 10 Plan 09: AutoTBATS, Intermittent, Panel, Laplace, GARCH, and Ensemble Reference Pages Summary

**10 per-model reference pages (AutoTBATS, IMAPA, TSB, GlobalCroston, GlobalETS, Laplace, GARCH, AutoEnsemble, explicit-member ensemble, ensemble introspection) rewritten to document Phase 10's crate-native interval passthrough, strict case-insensitive panel-parameter validation, and real tested seasonal_period semantics — closing the per-model reference-page sweep and retiring the last 53 "(being corrected in v0.10.0)" placeholder notes phase-wide.**

## Performance

- **Duration:** ~1h (single session; 2 task commits + this metadata commit)
- **Tasks:** 2 (both completed as planned, no checkpoints)
- **Files modified:** 25 (10 reference pages, 13 out-of-scope docs/skills touched by the required sweep, 1 synopsis.txt, 2 new pass-list files)

## Accomplishments

- **Task 1 (tracer) — AutoTBATS, IMAPA, TSB, GlobalCroston, GlobalETS:** `auto_tbats.md` rewritten
  with the crate-interval Returns paragraph (matching 10-08's `tbats.md` precedent), the
  too-narrow known issue (3-8% of `statsforecast` width, Phase 11), and the period
  auto-detect-or-error rule — also fixed a stale `ts_detect_periods_by` workaround reference
  to the working `ts_detect_periods_agg` aggregate form. `imapa.md`/`tsb.md` got the same
  crate-interval text plus the intermittent-demand interval-calibration caveat established in
  10-07's Croston/ADIDA pages; `imapa.md`'s undefined `intermittent_demand`/`spare_parts`
  table references were fixed to the shared `intermittent_sales`/`spare_parts_orders`
  fixtures. `global_croston.md` documents `croston_variant` as case-insensitive `'classic'`
  or `'sba'` (default classic), any other value an `InvalidInput` error — proven with a
  misspelled-variant (`'sab'`) example executed against the built extension.
  `global_ets.md` documents `model_pool` as case-insensitive `'reduced'`/`'complete'`, any
  other value an `InvalidParameter` error — proven with a misspelled (`'compete'`) example;
  also fixed a pre-existing undefined `'panel'` table reference in its non-seasonal example.
- **Task 2 — Laplace, GARCH, ensemble pages, phase-wide sweep:** `laplace.md` now states its
  bounds are the forecaster's own distributional interval (level clamped to `[0.5, 0.999]`),
  not overwritten by a generic heuristic; fixed four pre-existing undefined table references
  (`sales_monthly`, `sales_daily`, `train`/`test`) with self-contained inline fixtures.
  `garch.md` documents the D-01 interval passthrough with an explicit, tested caveat: `yhat`
  stays the analytical conditional standard deviation while `yhat_lower`/`yhat_upper` center a
  `statsforecast`-style band on GARCH's *simulated* innovation path — a different point
  forecast — so the bounds do not bracket `yhat` (confirmed with a `brackets_yhat` example
  showing at least one `false` row); also fixed an undefined `price_history` table reference.
  `autoensemble.md` documents non-NULL crate `AutoEnsemble` intervals at `confidence_level`
  (default 0.90, new parameter + example); fixed an undefined `weekly_sales` table reference.
  `ensemble_explicit.md` documents the same for the crate `Ensemble`, adds a
  `confidence_level := 0.95` executed example, and adds an executed `['ETS','ETS']`-equals-
  standalone-`ETS` identity example (`mismatched_steps = 0`). Both `ensemble_explicit.md` and
  `ensemble_inspect.md`'s `seasonal_period` documentation was corrected after direct testing
  against the built extension (see Deviations) — the pre-existing "falls back to `period=12`"
  text for seasonal-required members was also replaced with the real tested behaviour: a hard
  error naming the model.
- **Phase-wide correction-note sweep:** `grep -rl 'being corrected in v0.10.0' docs/ plugins/
  README.md` found 53 remaining occurrences across 13 files entirely outside this plan's own
  scope (statistics, diagnostics, feature extraction, hierarchical decomposition, conformal
  prediction, data-prep docs and skills) — each one a real, still-open issue explicitly
  tracked for a specific future phase (11 through 17). Reworded every occurrence from
  `"(being corrected in v0.10.0, Phase N)"` to `"(tracked for Phase N)"`, preserving every
  substantive caveat while retiring the literal marker phrase the plan's own acceptance
  criterion checks for repo-wide.

## Task Commits

1. **Task 1: Tracer — AutoTBATS, IMAPA, TSB, GlobalCroston, GlobalETS pages updated and executed** — `04a67ae` (docs)
2. **Task 2: Laplace, GARCH and ensemble pages; phase-wide correction-note sweep** — `c3e543c` (docs)

**Plan metadata:** (this commit, immediately following)

## Files Created/Modified

- `docs/reference/models/multi-seasonal/auto_tbats.md` — crate-interval Returns rewrite,
  too-narrow known issue, period auto-detect-or-error rule, `ts_detect_periods_agg` fix.
- `docs/reference/models/intermittent/{imapa,tsb,global_croston}.md` — crate-interval Returns
  rewrite (IMAPA/TSB), `croston_variant` case-insensitive/strict documentation + error example
  (GlobalCroston), example table-reference fixes.
- `docs/reference/models/exponential-smoothing/global_ets.md` — `model_pool`
  case-insensitive/strict documentation + error example, example table-reference fix.
- `docs/reference/models/distributional/laplace.md` — distributional-interval passthrough
  Returns rewrite, example table-reference fixes.
- `docs/reference/models/classical/garch.md` — D-01 interval passthrough + non-bracketing
  caveat, executed demonstration example, undefined-table fix.
- `docs/reference/models/ensemble/{autoensemble,ensemble_explicit,ensemble_inspect}.md` —
  non-NULL interval documentation, `confidence_level` parameter + examples, `['ETS','ETS']`
  identity example, corrected `seasonal_period` semantics, undefined-table fix.
- `docs/api/{02-hierarchical,03-statistics,04-data-preparation,05a-decomposition,
  09-evaluation-metrics,10-diagnostics,11-conformal-prediction,20-feature-extraction,
  21-feature-reference}.md` and `plugins/anofox-forecast/skills/anofox-forecast-{backtest,
  data-prep,detection,eda}/SKILL.md` — phase-wide `(being corrected in v0.10.0, Phase N)` →
  `(tracked for Phase N)` rewording (53 occurrences, mechanical, no content deletion).
- `.planning/phases/10-forecast-wrapper-delegates-to-crate-models/snippets/synopsis.txt` — 18
  new SYNOPSIS entries (Signature blocks and deliberate-error examples).
- `.planning/phases/10-forecast-wrapper-delegates-to-crate-models/snippets/pass-list-10-09{,b}.tsv`
  — per-block execution records for this plan's two task groups.

## Decisions Made

See frontmatter `key-decisions`. Most significant: discovering and documenting that
`ts_forecast_ensemble_by`/`ts_ensemble_inspect_by`'s `seasonal_period` parameter does **not**
implement a separate "explicit `1` = non-seasonal" override, contradicting the plan's own
`<interfaces>` guidance — `seasonal_period := 0` and `seasonal_period := 1` produced bit-
identical results against the built extension on every test series (both trigger the same
auto-detection pass), while only `seasonal_period > 1` is honoured literally. Per this plan's
own hard constraint ("describe only behaviour the built extension actually shows"), the
reference pages document the tested behaviour rather than the assumed one.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Nine pre-existing undefined-table references across Task 1/2 pages**
- **Found during:** Both tasks, running `run_snippets.py --all`
- **Issue:** `imapa.md` (`intermittent_demand`, `spare_parts`), `global_ets.md` (`panel`),
  `laplace.md` (`sales_monthly`, `sales_daily`, `train`, `test`), `garch.md`
  (`price_history`), and `autoensemble.md` (`weekly_sales`) all referenced tables never
  created anywhere in the shared `fixtures.sql` or the page itself.
- **Fix:** Pointed `imapa.md` at the existing `intermittent_sales`/`spare_parts_orders`
  fixtures; added self-contained inline `CREATE OR REPLACE TABLE` fixtures for the rest,
  following the pattern established in 10-06/10-07/10-08.
- **Files modified:** `docs/reference/models/intermittent/imapa.md`,
  `docs/reference/models/exponential-smoothing/global_ets.md`,
  `docs/reference/models/distributional/laplace.md`,
  `docs/reference/models/classical/garch.md`,
  `docs/reference/models/ensemble/autoensemble.md`
- **Verification:** `run_snippets.py --all` on each affected file: 0 FAIL.
- **Committed in:** `04a67ae` (Task 1), `c3e543c` (Task 2)

**2. [Rule 1 - Doc correctness] ensemble_explicit.md/ensemble_inspect.md's seasonal_period claim corrected after testing**
- **Found during:** Task 2, verifying the plan's `<interfaces>` claim ("explicit 1 =
  non-seasonal") before writing it into the pages
- **Issue:** Direct comparison against the built extension showed `seasonal_period := 0`,
  `:= 1`, and omitted all produce identical results for the same series/members (all three
  trigger the same auto-detection pass), while `:= 2`/`:= 7` differ — there is no
  "explicit 1 forces non-seasonal" code path in `forecast_explicit_ensemble`/
  `inspect_explicit_ensemble` (confirmed by reading `crates/anofox-fcst-core/src/forecast.rs`:
  `if period > 1 { period } else { detect_seasonality(...) }`, with a stale doc comment on
  `inspect_explicit_ensemble` itself claiming "0 or 1 → no seasonality" that does not match
  the code). A `HoltWinters` ensemble member with `seasonal_period := 1` on a non-seasonal
  series raised the same "requires a seasonal period > 1" error as `:= 0`, not a silent
  non-seasonal fit.
- **Fix:** Documented the tested behaviour (0/1 both auto-detect; only `>1` wins) instead of
  the plan's assumed claim; also replaced the pre-existing "falls back to `period=12`" text
  for seasonal-required Tier-2 members with the real tested behaviour (hard error naming the
  model).
- **Files modified:** `docs/reference/models/ensemble/ensemble_explicit.md`,
  `docs/reference/models/ensemble/ensemble_inspect.md`
- **Verification:** Direct `duckdb` queries comparing `seasonal_period := 0/1/2/7` on
  identical series/members, and a `HoltWinters` error-path query, all run against the built
  extension; `run_snippets.py --all` on both files: 0 FAIL.
- **Committed in:** `c3e543c` (Task 2)

**3. [Rule 2 - Missing critical / plan-directed] Phase-wide `(being corrected in v0.10.0)` sweep across 13 out-of-scope files**
- **Found during:** Task 2, running the plan's own required sweep command
- **Issue:** 53 occurrences of the placeholder note remained outside this plan's (and 10-06
  through 10-08's) own `files_modified` lists, each one genuinely tracking an open issue for
  a future phase (11-17) — not stale phase-10 work, but still matching the plan's literal
  acceptance-criterion grep.
- **Fix:** Reworded every occurrence from `"(being corrected in v0.10.0, Phase N)"` to
  `"(tracked for Phase N)"` via a mechanical regex pass, preserving every substantive caveat
  verbatim and only retiring the marker phrase — per the plan's own explicit instruction
  ("if a file outside plans 10-06..10-09 still carries the note, fix it here and record the
  deviation in the SUMMARY").
- **Files modified:** `docs/api/{02-hierarchical,03-statistics,04-data-preparation,
  05a-decomposition,09-evaluation-metrics,10-diagnostics,11-conformal-prediction,
  20-feature-extraction,21-feature-reference}.md`,
  `plugins/anofox-forecast/skills/anofox-forecast-{backtest,data-prep,detection,eda}/SKILL.md`
- **Verification:** `grep -rl 'being corrected in v0.10.0' docs/ plugins/ README.md` returns
  empty; spot-checked diffs for double-parens/broken-markdown artifacts (none found); two
  manually cleaned up for grammar (`ts_mape`/`ts_mase` rows in the backtest skill, where the
  original had no enclosing parens).
- **Committed in:** `c3e543c` (Task 2)

### Deferred

None — every discovered issue in this plan's own pages, and the plan-mandated phase-wide
sweep, was fixed and verified before commit.

---

**Total deviations:** 3 auto-fixed (1 Rule 1 — pre-existing doc-rot across 5 files; 1 Rule 1 —
a doc-correctness finding that overrides the plan's own assumed interfaces text; 1 Rule 2 —
the plan's own explicitly-mandated phase-wide sweep, scoped beyond this plan's
`files_modified` but directly instructed by the plan's Task 2 action text), 0 deferred.
**Impact on plan:** No unauthorized scope creep — the sweep was explicitly directed by the
plan itself, and the seasonal_period correction follows the plan's own overriding constraint
("describe only behaviour the built extension actually shows") when it conflicted with the
plan's own assumed interfaces note.

## Issues Encountered

- The plan's `<interfaces>` block stated "explicit 1 = non-seasonal" for
  `ts_forecast_ensemble_by`/`ts_ensemble_inspect_by`, but direct testing against the built
  extension showed no such behaviour exists at this surface (see Deviation 2). Resolved by
  documenting the tested reality and noting the discrepancy here rather than silently
  transcribing an unverified claim.
- `grep -c 'conditional standard deviation'` initially returned 0 against a first draft of
  `garch.md` because the phrase was split by markdown bold markers (`conditional
  **standard deviation**`) — the acceptance criterion matches the literal substring. Fixed by
  removing the bold markers from that one occurrence (the term is still emphasized elsewhere
  in the page).

## Known Stubs

None — all 10 reference pages describe behaviour verified against the built extension; no
placeholder text, hardcoded empty values, or unwired examples.

## Threat Flags

None beyond what the plan's own `<threat_model>` already covers (T-10-22) — GARCH's
non-bracketing interval semantics are documented with an explicit caveat and an executed
example proving the non-bracketing claim, with a conformal-intervals pointer for calibrated
use.

## User Setup Required

None.

## Next Phase Readiness

- Phase 10's per-model reference-page sweep (D-13) is complete: no file in `docs/reference/
  models/**` carries a Phase-10 correction note, and the broader `(being corrected in
  v0.10.0, ...)` placeholder pattern has been fully retired from `docs/`, `plugins/`, and
  `README.md` — any future phase (11 through 17) that resolves one of the 53 reworded
  caveats should search for `"(tracked for Phase N)"` with that phase's number, not the old
  marker phrase.
- The phase-local snippet harness (`.../10-forecast-wrapper-delegates-to-crate-models/
  snippets/`) remains unmodified and available for later phases that want to reuse the
  pattern (`run_snippets.py --all --out <path> FILE...`).
- Flagging for Phase 11/17 (upstream crate correctness / R-parity owners): this plan's
  `ensemble_explicit.md`/`ensemble_inspect.md` finding — `seasonal_period` 0 and 1 are
  currently indistinguishable at the Rust API level (`forecast_explicit_ensemble`/
  `inspect_explicit_ensemble` in `crates/anofox-fcst-core/src/forecast.rs`), and
  `inspect_explicit_ensemble`'s own doc comment claims otherwise — a documentation/code
  mismatch inside the crate crate itself, out of scope for this docs-only plan to fix (no
  `src/`/`crates/` changes permitted), but worth resolving alongside any future touch of that
  function.

---
*Phase: 10-forecast-wrapper-delegates-to-crate-models*
*Completed: 2026-10-09*

## Self-Check: PASSED

All 10 claimed reference pages, both pass-list files, and this SUMMARY found on disk; both
claimed commits (`04a67ae`, `c3e543c`) found in `git log --oneline --all`.
