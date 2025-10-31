-- Step 1: Start with Auto
CREATE TABLE auto_forecast AS
SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, 
                          {'seasonal_period': 7, 'return_insample': true});

-- Step 2: Check fit quality
SELECT 
    TS_R2(LIST(actual), insample_fitted) AS r_squared,
    model_name
FROM auto_forecast, actuals;

-- Step 3: If not satisfied, try manual tuning
CREATE TABLE manual_forecast AS
SELECT * FROM TS_FORECAST('sales', date, amount, 'ETS', 28, {
    'seasonal_period': 7,
    'error_type': 1,      -- Try multiplicative
    'trend_type': 2,      -- Try damped
    'season_type': 1,
    'return_insample': true
});

-- Step 4: Compare
SELECT 
    'AutoETS' AS approach,
    TS_R2(LIST(actual), (SELECT insample_fitted FROM auto_forecast)) AS r2
FROM actuals
UNION ALL
SELECT 
    'Manual ETS',
    TS_R2(LIST(actual), (SELECT insample_fitted FROM manual_forecast))
FROM actuals;
