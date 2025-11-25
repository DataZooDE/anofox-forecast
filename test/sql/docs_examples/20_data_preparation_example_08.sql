-- Overall statistics
SELECT * FROM TS_DATASET_SUMMARY('stats');

-- Example output:
-- | total_series | total_observations | avg_series_length | date_span | frequency |
-- |--------------|-------------------|------------------|-----------|-----------|
-- | 1000         | 365000            | 365.0             | 730       | Daily     |
