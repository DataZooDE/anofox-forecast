-- All checks in one report
SELECT * FROM TS_QUALITY_REPORT('stats', 30);

-- Example output:
-- | check_type              | total_series | series_with_gaps | pct_with_gaps |
-- |-------------------------|--------------|------------------|---------------|
-- | Gap Analysis            | 1000         | 150              | 15.0%         |
-- | Missing Values          | 1000         | 45               | 4.5%          |
-- | Constant Series         | 1000         | 23               | 2.3%          |
-- | Short Series (< 30)     | 1000         | 67               | 6.7%          |
-- | End Date Alignment      | 1000         | 892              | 11 rows       |
