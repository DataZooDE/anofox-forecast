-- Use seasonality analyzer for ACF
SELECT * FROM TS_ANALYZE_SEASONALITY(
    LIST(date ORDER BY date),
    LIST(amount ORDER BY date)
)
FROM sales;

-- Returns: trend_strength, seasonal_strength, etc.
