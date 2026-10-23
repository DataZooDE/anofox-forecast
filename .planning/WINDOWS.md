---
schema_version: 1
open_count: 6
waived_count: 0
fixed_count: 0
total_count: 6
last_updated: 2026-10-23T15:01:26.530Z
---

# Broken Windows Ledger

> Cross-phase defect register. With `workflow.windows_enforce` enabled, `/gsd-ship` blocks while `open_count > 0`.
> Waive with `gsd-tools windows waive <id> "<reason>"` (reason required).
> Mark fixed with `gsd-tools windows fixed <id>`.

| id | phase | kind | file | line | description | status | reason | recorded_at | resolved_at |
|----|-------|------|------|------|-------------|--------|--------|-------------|-------------|
| 1 | 1 | stub | docs/api/10-diagnostics.md |  | Placeholder stubs for ts_kpss, ts_stationarity, ts_ljung_box, ts_durbin_watson, ts_jarque_bera, ts_residual_diagnostics_by (Plans 01-2/01-3) | open |  | 2026-08-21T10:09:15.852Z |  |
| 2 | 10 | unmet-truth | test/sql/ts_model_distinctness.test | 101 | SeasonalES/SeasonalESOptimized expected values stale since Plan 10-03's resolve_period change (e01e87b); never re-blessed. Discovered during Plan 10-04 Task 3 full-suite run; not caused by 10-04. | open |  | 2026-10-09T15:05:43.862Z |  |
| 3 | 17 | deviation | test/sql/ts_model_distinctness.test | 126 | Pre-existing MSTL point-value drift (18.18416 -> 18.186245) found while re-blessing the Holt row for D-13; unrelated to the Holt remap, re-pinned to the measured value (deferred-items.md). | open |  | 2026-10-15T06:03:20.661Z |  |
| 4 | 17 | deviation | docs/api/07-forecasting.md |  | Pre-existing snippet-harness FAIL (sales_data example, untouched block) found during Task 4's --all sweep; reproduces identically on pre-Task-4 HEAD, not fixed (out of scope, deferred-items.md). | open |  | 2026-10-15T06:03:25.112Z |  |
| 5 | 20 | unrun-verify | .github/workflows/MainDistributionPipeline.yml |  | 20-03 Task 3: branch MainDistributionPipeline (wasm-runtime-test on v1.5.6 curated subset incl. fill-forward pins; v1.4.5/v1.5.6/v2.0.0 builds) not run - pipeline does not trigger on branch push and a workflow_dispatch uploads to the bucket; needs user dispatch or the PR run | open |  | 2026-10-23T14:11:39.075Z |  |
| 6 | 20 | unrun-verify | src/macros/ts_macros.cpp |  | 20-04: wasm-runtime-test and v1.4.5/v1.5.6/v2.0.0 MainDistributionPipeline jobs not run for the 7 feature/CV/panel/VAR macro changes; deferred to the end-of-Phase-20 dispatch | open |  | 2026-10-23T15:01:26.530Z |  |

````json
[
  {
    "id": 1,
    "kind": "stub",
    "phase": "1",
    "file": "docs/api/10-diagnostics.md",
    "line": null,
    "description": "Placeholder stubs for ts_kpss, ts_stationarity, ts_ljung_box, ts_durbin_watson, ts_jarque_bera, ts_residual_diagnostics_by (Plans 01-2/01-3)",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-21T10:09:15.852Z",
    "resolved_at": null
  },
  {
    "id": 2,
    "kind": "unmet-truth",
    "phase": "10",
    "file": "test/sql/ts_model_distinctness.test",
    "line": 101,
    "description": "SeasonalES/SeasonalESOptimized expected values stale since Plan 10-03's resolve_period change (e01e87b); never re-blessed. Discovered during Plan 10-04 Task 3 full-suite run; not caused by 10-04.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-10-09T15:05:43.862Z",
    "resolved_at": null
  },
  {
    "id": 3,
    "kind": "deviation",
    "phase": "17",
    "file": "test/sql/ts_model_distinctness.test",
    "line": 126,
    "description": "Pre-existing MSTL point-value drift (18.18416 -> 18.186245) found while re-blessing the Holt row for D-13; unrelated to the Holt remap, re-pinned to the measured value (deferred-items.md).",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-10-15T06:03:20.661Z",
    "resolved_at": null
  },
  {
    "id": 4,
    "kind": "deviation",
    "phase": "17",
    "file": "docs/api/07-forecasting.md",
    "line": null,
    "description": "Pre-existing snippet-harness FAIL (sales_data example, untouched block) found during Task 4's --all sweep; reproduces identically on pre-Task-4 HEAD, not fixed (out of scope, deferred-items.md).",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-10-15T06:03:25.112Z",
    "resolved_at": null
  },
  {
    "id": 5,
    "kind": "unrun-verify",
    "phase": "20",
    "file": ".github/workflows/MainDistributionPipeline.yml",
    "line": null,
    "description": "20-03 Task 3: branch MainDistributionPipeline (wasm-runtime-test on v1.5.6 curated subset incl. fill-forward pins; v1.4.5/v1.5.6/v2.0.0 builds) not run - pipeline does not trigger on branch push and a workflow_dispatch uploads to the bucket; needs user dispatch or the PR run",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-10-23T14:11:39.075Z",
    "resolved_at": null
  },
  {
    "id": 6,
    "kind": "unrun-verify",
    "phase": "20",
    "file": "src/macros/ts_macros.cpp",
    "line": null,
    "description": "20-04: wasm-runtime-test and v1.4.5/v1.5.6/v2.0.0 MainDistributionPipeline jobs not run for the 7 feature/CV/panel/VAR macro changes; deferred to the end-of-Phase-20 dispatch",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-10-23T15:01:26.530Z",
    "resolved_at": null
  }
]
````
