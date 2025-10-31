-- For each model, verify it returns forecasts
SELECT TS_FORECAST(
    value, 
    'ModelName', 
    12,  -- horizon
    {}   -- params
) FROM test_data;
