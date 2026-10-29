---
quick_id: 261029-vza
status: complete
issue: 305
---

# 261029-vza: ts_aggregate_hierarchy flat-array aggregation (#305)

Implemented as planned in `src/table_functions/ts_aggregate_hierarchy.cpp`. Executed inline.

## Results (10,000 series x 365 days, 3 key levels, DuckDB v2.0.0 build, shared machine)
| | before | after |
|---|---:|---:|
| `ts_aggregate_hierarchy` (macro) | 11.9-18.9 s | 1.1-1.3 s |
| `_ts_aggregate_hierarchy_native` alone | n/a | 0.84 s |
| `ORDER BY ALL` input sort (union safety) | | 0.14 s |
| `GROUP BY ds, ROLLUP(...)` reference | | 0.11 s |

What remains is single-threaded per-row key interning: the native function is `MaxThreads() = 1` behind the #285 finalize barrier. Closing the last ~10x would need the issue's option 1, a bind-time ROLLUP rewrite.

## Verification
- `261029-vza-hierarchy-probe.sql` gives identical output before and after:
  - count and hash on the large panel: 3,727,015 rows
  - an order-sensitive hash on a small panel with NULL values, NULL keys, BIGINT keys, TIMESTAMP dates, a custom separator and a custom keyword
- Full sqllogictest suite: 6962 assertions in 109 test cases, all pass. That includes ts_aggregate_hierarchy.test, ts_multi_key.test and ts_grouping_sets.test.
- Built for v2.0.0 only, locally. The v1.5.6 line uses the same compat helpers (`ToUnified`, `FlatDataMut`); CI covers it.
