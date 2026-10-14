
- **ts_coverage_by / ts_quantile_loss_by (found in 16-03, pre-existing):** the macro bodies pass `source` straight into a TABLE-typed native parameter without a `query_table()` wrap, so the macros fail. Not a Phase 16 requirement. Target: Phase 18 (TEST-03 / docs sweep).
