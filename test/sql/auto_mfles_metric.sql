-- Test AutoMFLES with different metric options
-- This tests the metric parameter for optimization

-- Create test data
CREATE OR REPLACE TABLE test_data AS
SELECT 
    i AS ds,
    10 + 5 * sin(2 * pi() * i / 12) + random() * 2 AS y
FROM range(1, 101) t(i);

-- Test 1: Default metric (MAE)
statement ok
CREATE OR REPLACE TABLE forecast_mae AS
SELECT * FROM TS_FORECAST(ds, y, 'AutoMFLES', 12, MAP{}) 
FROM test_data;

-- Verify forecast was created
query I
SELECT COUNT(*) FROM forecast_mae;
----
12

-- Test 2: Explicit MAE metric
statement ok
CREATE OR REPLACE TABLE forecast_mae_explicit AS
SELECT * FROM TS_FORECAST(ds, y, 'AutoMFLES', 12, MAP{'metric': 'mae'}) 
FROM test_data;

query I
SELECT COUNT(*) FROM forecast_mae_explicit;
----
12

-- Test 3: RMSE metric
statement ok
CREATE OR REPLACE TABLE forecast_rmse AS
SELECT * FROM TS_FORECAST(ds, y, 'AutoMFLES', 12, MAP{'metric': 'rmse'}) 
FROM test_data;

query I
SELECT COUNT(*) FROM forecast_rmse;
----
12

-- Test 4: MAPE metric
statement ok
CREATE OR REPLACE TABLE forecast_mape AS
SELECT * FROM TS_FORECAST(ds, y, 'AutoMFLES', 12, MAP{'metric': 'mape'}) 
FROM test_data;

query I
SELECT COUNT(*) FROM forecast_mape;
----
12

-- Test 5: SMAPE metric
statement ok
CREATE OR REPLACE TABLE forecast_smape AS
SELECT * FROM TS_FORECAST(ds, y, 'AutoMFLES', 12, MAP{'metric': 'smape'}) 
FROM test_data;

query I
SELECT COUNT(*) FROM forecast_smape;
----
12

-- Test 6: SMAPE with multiple seasonalities
statement ok
CREATE OR REPLACE TABLE forecast_smape_multi AS
SELECT * FROM TS_FORECAST(ds, y, 'AutoMFLES', 12, 
    MAP{'metric': 'smape', 'seasonal_periods': [12]}) 
FROM test_data;

query I
SELECT COUNT(*) FROM forecast_smape_multi;
----
12

-- Test 7: RMSE with custom CV settings
statement ok
CREATE OR REPLACE TABLE forecast_rmse_cv AS
SELECT * FROM TS_FORECAST(ds, y, 'AutoMFLES', 12, 
    MAP{'metric': 'rmse', 'cv_horizon': 6, 'cv_n_windows': 3}) 
FROM test_data;

query I
SELECT COUNT(*) FROM forecast_rmse_cv;
----
12

-- Test 8: Invalid metric should fail
statement error
SELECT * FROM TS_FORECAST(ds, y, 'AutoMFLES', 12, MAP{'metric': 'invalid'}) 
FROM test_data;

-- Test 9: Metric with custom learning rates
statement ok
CREATE OR REPLACE TABLE forecast_mape_custom_lr AS
SELECT * FROM TS_FORECAST(ds, y, 'AutoMFLES', 12, 
    MAP{'metric': 'mape', 'lr_trend': 0.4, 'lr_season': 0.6, 'lr_rs': 0.9}) 
FROM test_data;

query I
SELECT COUNT(*) FROM forecast_mape_custom_lr;
----
12

-- Test 10: All parameters together
statement ok
CREATE OR REPLACE TABLE forecast_full_params AS
SELECT * FROM TS_FORECAST(ds, y, 'AutoMFLES', 12, 
    MAP{
        'metric': 'smape',
        'seasonal_periods': [12],
        'max_rounds': 8,
        'lr_trend': 0.35,
        'lr_season': 0.55,
        'lr_rs': 0.85,
        'cv_horizon': 6,
        'cv_n_windows': 2
    }) 
FROM test_data;

query I
SELECT COUNT(*) FROM forecast_full_params;
----
12

-- Cleanup
DROP TABLE test_data;
DROP TABLE forecast_mae;
DROP TABLE forecast_mae_explicit;
DROP TABLE forecast_rmse;
DROP TABLE forecast_mape;
DROP TABLE forecast_smape;
DROP TABLE forecast_smape_multi;
DROP TABLE forecast_rmse_cv;
DROP TABLE forecast_mape_custom_lr;
DROP TABLE forecast_full_params;

