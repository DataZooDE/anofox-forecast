-- Test MFLES algorithm modes (StatsForecast vs original AnoFox)
-- Tests progressive_trend and sequential_seasonality parameters

-- Create test data with trend and seasonality
CREATE OR REPLACE TABLE test_data AS
SELECT 
    i AS ds,
    10 + 0.5 * i + 5 * sin(2 * pi() * i / 12) + random() * 2 AS y
FROM range(1, 101) t(i);

-- Test 1: Default behavior (StatsForecast: progressive_trend=true, sequential_seasonality=true)
statement ok
CREATE OR REPLACE TABLE forecast_default AS
SELECT * FROM TS_FORECAST(ds, y, 'MFLES', 12, MAP{}) 
FROM test_data;

query I
SELECT COUNT(*) FROM forecast_default;
----
12

-- Test 2: Explicit StatsForecast mode
statement ok
CREATE OR REPLACE TABLE forecast_statsforecast AS
SELECT * FROM TS_FORECAST(ds, y, 'MFLES', 12, 
    MAP{'progressive_trend': true, 'sequential_seasonality': true}) 
FROM test_data;

query I
SELECT COUNT(*) FROM forecast_statsforecast;
----
12

-- Test 3: Original AnoFox mode (simultaneous seasonality, fixed trend)
statement ok
CREATE OR REPLACE TABLE forecast_anofox AS
SELECT * FROM TS_FORECAST(ds, y, 'MFLES', 12, 
    MAP{'progressive_trend': false, 'sequential_seasonality': false}) 
FROM test_data;

query I
SELECT COUNT(*) FROM forecast_anofox;
----
12

-- Test 4: Progressive trend only
statement ok
CREATE OR REPLACE TABLE forecast_prog_trend AS
SELECT * FROM TS_FORECAST(ds, y, 'MFLES', 12, 
    MAP{'progressive_trend': true, 'sequential_seasonality': false}) 
FROM test_data;

query I
SELECT COUNT(*) FROM forecast_prog_trend;
----
12

-- Test 5: Sequential seasonality only
statement ok
CREATE OR REPLACE TABLE forecast_seq_season AS
SELECT * FROM TS_FORECAST(ds, y, 'MFLES', 12, 
    MAP{'progressive_trend': false, 'sequential_seasonality': true}) 
FROM test_data;

query I
SELECT COUNT(*) FROM forecast_seq_season;
----
12

-- Test 6: Multiple seasonalities with sequential mode
statement ok
CREATE OR REPLACE TABLE forecast_multi_seq AS
SELECT * FROM TS_FORECAST(ds, y, 'MFLES', 12, 
    MAP{'seasonal_periods': [12], 'sequential_seasonality': true}) 
FROM test_data;

query I
SELECT COUNT(*) FROM forecast_multi_seq;
----
12

-- Test 7: Multiple seasonalities with simultaneous mode
statement ok
CREATE OR REPLACE TABLE forecast_multi_simul AS
SELECT * FROM TS_FORECAST(ds, y, 'MFLES', 12, 
    MAP{'seasonal_periods': [12], 'sequential_seasonality': false}) 
FROM test_data;

query I
SELECT COUNT(*) FROM forecast_multi_simul;
----
12

-- Test 8: Verify forecasts are different between modes
-- StatsForecast vs Original should produce different results
query I
SELECT COUNT(*) FROM (
    SELECT d.point_forecast AS statsforecast_fc, a.point_forecast AS anofox_fc
    FROM forecast_default d
    JOIN forecast_anofox a ON d.forecast_step = a.forecast_step
    WHERE ABS(d.point_forecast - a.point_forecast) > 0.01
);
----
12

-- Test 9: Progressive trend with custom learning rates
statement ok
CREATE OR REPLACE TABLE forecast_custom AS
SELECT * FROM TS_FORECAST(ds, y, 'MFLES', 12, 
    MAP{
        'progressive_trend': true,
        'sequential_seasonality': true,
        'lr_trend': 0.4,
        'lr_season': 0.6,
        'lr_level': 0.9
    }) 
FROM test_data;

query I
SELECT COUNT(*) FROM forecast_custom;
----
12

-- Test 10: Verify all forecasts are valid (no NULLs or infinities)
query I
SELECT COUNT(*) FROM forecast_default 
WHERE point_forecast IS NOT NULL 
  AND point_forecast > -1e10 
  AND point_forecast < 1e10;
----
12

-- Cleanup
DROP TABLE test_data;
DROP TABLE forecast_default;
DROP TABLE forecast_statsforecast;
DROP TABLE forecast_anofox;
DROP TABLE forecast_prog_trend;
DROP TABLE forecast_seq_season;
DROP TABLE forecast_multi_seq;
DROP TABLE forecast_multi_simul;
DROP TABLE forecast_custom;

