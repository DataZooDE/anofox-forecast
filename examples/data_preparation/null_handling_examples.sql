-- ============================================================================
-- NULL Handling Examples — Phase 12 (RUST-01/RUST-02)
-- ============================================================================
-- Proves, against the BUILT extension, that:
--   - Paired metrics use pairwise-complete observations computed in Rust
--     (D-02): a pair/row is dropped the moment ANY input is NULL at that
--     position, not spliced independently per array in C++.
--   - Constant/literal list arguments are read correctly on EVERY row
--     (D-01), not just the first row of a chunk.
--
-- Every section ends with a self-checking assertion of the form
--   SELECT CASE WHEN <condition> THEN 'PASS <label>' ELSE error('FAIL ...') END;
-- so a wrong result aborts the whole script under -bail instead of silently
-- printing a bad number.
--
-- Later Phase 12 plans append their own sections before the final sentinel.
--
-- Run: ./build/release/duckdb -unsigned -bail < examples/data_preparation/null_handling_examples.sql
-- ============================================================================

LOAD anofox_forecast;

.print '============================================================================='
.print 'NULL HANDLING EXAMPLES — Phase 12 (RUST-01/RUST-02)'
.print '============================================================================='

-- ============================================================================
-- SECTION 1 — Paired metrics: pairwise-complete NULL policy (RUST-02, D-02)
-- ============================================================================

.print ''
.print 'SECTION 1: Paired metrics use pairwise-complete observations'
.print ''

-- ts_mae([1,NULL,3,4],[1,2,NULL,4]): complete pairs are (1,1) and (4,4) -> MAE=0.
-- Before this phase, each array's NULLs were dropped independently in C++
-- (misaligning the pairs to [1,3,4] vs [1,2,4]), giving 0.333.
SELECT CASE WHEN (
    SELECT ts_mae([1, NULL, 3, 4]::DOUBLE[], [1, 2, NULL, 4]::DOUBLE[]) = 0.0
) THEN 'PASS pairwise_complete_mae' ELSE error(
    'FAIL pairwise_complete_mae: got ' ||
    (SELECT ts_mae([1, NULL, 3, 4]::DOUBLE[], [1, 2, NULL, 4]::DOUBLE[]))::VARCHAR
) END AS result;

-- No complete pair remaining -> SQL NULL, not an error (D-04).
SELECT CASE WHEN (
    SELECT ts_mae([NULL, 1]::DOUBLE[], [1, NULL]::DOUBLE[]) IS NULL
) THEN 'PASS no_complete_pair_is_null' ELSE error('FAIL no_complete_pair_is_null') END AS result;

-- ============================================================================
-- SECTION 2 — Constant list arguments correct on every row (RUST-01, D-01)
-- ============================================================================

.print ''
.print 'SECTION 2: Constant/literal list arguments are correct on every row'
.print ''

CREATE OR REPLACE TABLE five AS SELECT i, [i, i + 1, i + 2]::DOUBLE[] AS a FROM range(5) t(i);

-- ts_mae(a, [0,0,0]) broadcasts a constant second argument across every row.
-- Before this phase, only row 0 of each chunk was read correctly; every
-- subsequent row returned NULL/garbage.
SELECT CASE WHEN (
    SELECT count(*) FROM five WHERE ts_mae(a, [0, 0, 0]::DOUBLE[]) = i + 1
) = 5 THEN 'PASS constant_list_correct_every_row' ELSE error(
    'FAIL constant_list_correct_every_row: got ' ||
    (SELECT count(*) FROM five WHERE ts_mae(a, [0, 0, 0]::DOUBLE[]) = i + 1)::VARCHAR || ' of 5'
) END AS result;

.print ''
.print '============================================================================='
.print 'ALL SECTIONS PASSED'
.print '============================================================================='
