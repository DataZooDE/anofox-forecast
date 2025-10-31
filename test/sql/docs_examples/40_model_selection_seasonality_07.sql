-- Hourly data with daily (24) and weekly (168) patterns
SELECT * FROM TS_FORECAST('hourly_sales', timestamp, amount, 'AutoMSTL', 168, {
    'seasonal_periods': [24, 168]  -- Daily and weekly
});
