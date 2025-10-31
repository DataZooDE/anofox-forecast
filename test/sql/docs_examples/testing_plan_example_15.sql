WITH normalized AS (
    SELECT * FROM TS_NORMALIZE('test_table', 'date_col', 'value_col', 'id_col', 'zscore')
)
SELECT * FROM TS_DENORMALIZE(normalized, ...);
-- Verify round-trip transformation
