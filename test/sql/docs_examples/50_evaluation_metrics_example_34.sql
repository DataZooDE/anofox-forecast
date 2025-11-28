-- Create sample forecast comparison data
CREATE TABLE forecast_comparison AS
SELECT 
    [100.0, 102.0, 98.0, 105.0]::DOUBLE[] AS actual,
    [101.0, 103.0, 99.0, 106.0]::DOUBLE[] AS forecast_autoets,
    [100.0, 100.0, 100.0, 100.0]::DOUBLE[] AS forecast_naive;

-- Compare AutoETS vs Naive forecast
SELECT 
    TS_MAE(actual, forecast_autoets) AS mae_autoets,
    TS_MAE(actual, forecast_naive) AS mae_naive,
    TS_RMAE(actual, forecast_autoets, forecast_naive) AS relative_performance,
    CASE 
        WHEN TS_RMAE(actual, forecast_autoets, forecast_naive) < 1.0
        THEN 'AutoETS is better'
        ELSE 'Naive is better'
    END AS winner
FROM forecast_comparison;

-- Result: 
-- mae_autoets: 2.0, mae_naive: 4.5, relative_performance: 0.44, winner: 'AutoETS is better'
