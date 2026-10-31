---
phase: quick-261029-vza
plan: 01
type: execute
requirements: [GH-305]
---

# 261029-vza: ts_aggregate_hierarchy flat-array aggregation (#305)

Executed inline (no sub-agents).

**Goal:** close most of the ~55x gap to `GROUP BY ROLLUP` while keeping the API (Phase 20.1 gate), the union-safe macro, and byte-identical output (rows, order, sums).

**Task 1:** rewrite collect/finalize/output in `src/table_functions/ts_aggregate_hierarchy.cpp`.
- Read the vectors directly through UnifiedVectorFormat for the common types (DATE/TIMESTAMP/TIMESTAMP_TZ/INTEGER/BIGINT dates, DOUBLE values, VARCHAR/INTEGER/BIGINT keys). Other types keep the previous Value conversions.
- Intern each id tuple as a leaf, using a length-prefixed key.
- Build every level's unique_id once per leaf, and give each distinct unique_id string a node.
- Stable-sort rows by date, which preserves input order within a date, so each cell sums in the same order as before. Accumulate per date block through node-indexed arrays.
- Emit with a counting sort by node uid order. Write output to flat vectors.

**Verify:**
- An order-sensitive hash of the output (row number, unique_id, date, value) is identical before and after, on the 10k x 365 x 3-level panel and on a small panel with NULL values, NULL keys, BIGINT keys and TIMESTAMP dates.
- The full sqllogictest suite passes.
