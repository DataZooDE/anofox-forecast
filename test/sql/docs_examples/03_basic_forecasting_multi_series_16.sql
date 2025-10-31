-- Step 1: Start with AutoETS
SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, {'seasonal_period': 7});

-- Step 2: If needed, try specialized models
SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoARIMA', 28, {'seasonal_period': 7});

-- Step 3: Fine-tune parameters
SELECT * FROM TS_FORECAST('sales', date, amount, 'ETS', 28, 
                          {'seasonal_period': 7, 'trend_type': 2, 'season_type': 1});
