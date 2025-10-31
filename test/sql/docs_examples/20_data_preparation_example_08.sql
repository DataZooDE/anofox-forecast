-- Overall statistics
SELECT * FROM TS_DATASET_SUMMARY('stats');

-- Example output:
-- | total_series | total_observations | avg_series_length | date_span_days |
-- |--------------|-------------------|-------------------|----------------|
-- | 1000         | 365000            | 365.0             | 730            |

-- Find problematic series
SELECT * FROM TS_GET_PROBLEMATIC('stats', 0.5);  -- quality_score < 0.5
