---
schema_version: 1
open_count: 2
waived_count: 0
fixed_count: 0
total_count: 2
last_updated: 2026-10-09T15:05:43.862Z
---

# Broken Windows Ledger

> Cross-phase defect register. With `workflow.windows_enforce` enabled, `/gsd-ship` blocks while `open_count > 0`.
> Waive with `gsd-tools windows waive <id> "<reason>"` (reason required).
> Mark fixed with `gsd-tools windows fixed <id>`.

| id | phase | kind | file | line | description | status | reason | recorded_at | resolved_at |
|----|-------|------|------|------|-------------|--------|--------|-------------|-------------|
| 1 | 1 | stub | docs/api/10-diagnostics.md |  | Placeholder stubs for ts_kpss, ts_stationarity, ts_ljung_box, ts_durbin_watson, ts_jarque_bera, ts_residual_diagnostics_by (Plans 01-2/01-3) | open |  | 2026-08-21T10:09:15.852Z |  |
| 2 | 10 | unmet-truth | test/sql/ts_model_distinctness.test | 101 | SeasonalES/SeasonalESOptimized expected values stale since Plan 10-03's resolve_period change (e01e87b); never re-blessed. Discovered during Plan 10-04 Task 3 full-suite run; not caused by 10-04. | open |  | 2026-10-09T15:05:43.862Z |  |

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
  }
]
````
