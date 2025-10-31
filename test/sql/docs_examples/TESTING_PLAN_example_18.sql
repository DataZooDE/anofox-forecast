-- Test forecasting with many groups
SELECT series_id, result.* 
FROM ts_forecast_by(
    large_dataset,
    'date',
    'value', 
    'series_id',
    'Naive',
    12,
    {}
);
-- Verify all groups get forecasts
