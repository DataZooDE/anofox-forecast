-- Test AutoTheta basic functionality
-- AutoTheta automatically selects the best Theta variant (STM, OTM, DSTM, DOTM)

-- Create test data
CREATE TABLE test_theta_data AS 
SELECT 
    DATE '2020-01-01' + INTERVAL (i) DAY AS date,
    100.0 + i * 0.5 + (i % 7) * 10.0 AS value
FROM range(0, 60) t(i);

-- Test 1: Basic AutoTheta forecast
SELECT 
    'AutoTheta Basic' as test,
    COUNT(*) as forecast_length,
    MIN(forecast) as min_forecast,
    MAX(forecast) as max_forecast,
    AVG(forecast) as avg_forecast
FROM TS_FORECAST('test_theta_data', date, value, 'AutoTheta', 7, {'seasonal_period': 7});

-- Test 2: AutoTheta with specific model selection (OTM)
SELECT 
    'AutoTheta with OTM' as test,
    COUNT(*) as forecast_length
FROM TS_FORECAST('test_theta_data', date, value, 'AutoTheta', 7, 
    {'seasonal_period': 7, 'model': 'OTM'});

-- Test 3: AutoTheta with specific model selection (DOTM)
SELECT 
    'AutoTheta with DOTM' as test,
    COUNT(*) as forecast_length
FROM TS_FORECAST('test_theta_data', date, value, 'AutoTheta', 7, 
    {'seasonal_period': 7, 'model': 'DOTM'});

-- Test 4: AutoTheta with additive decomposition
SELECT 
    'AutoTheta additive' as test,
    COUNT(*) as forecast_length
FROM TS_FORECAST('test_theta_data', date, value, 'AutoTheta', 7, 
    {'seasonal_period': 7, 'decomposition_type': 'additive'});

-- Test 5: AutoTheta with multiplicative decomposition
SELECT 
    'AutoTheta multiplicative' as test,
    COUNT(*) as forecast_length
FROM TS_FORECAST('test_theta_data', date, value, 'AutoTheta', 7, 
    {'seasonal_period': 7, 'decomposition_type': 'multiplicative'});

-- Test 6: AutoTheta with custom nmse parameter
SELECT 
    'AutoTheta custom nmse' as test,
    COUNT(*) as forecast_length
FROM TS_FORECAST('test_theta_data', date, value, 'AutoTheta', 7, 
    {'seasonal_period': 7, 'nmse': 5});

-- Cleanup
DROP TABLE test_theta_data;

