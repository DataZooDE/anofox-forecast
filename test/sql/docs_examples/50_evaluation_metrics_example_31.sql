-- Create sample model comparison data with actuals and predictions
CREATE TABLE model_comparison AS
SELECT 
    'AutoETS' AS model_name,
    [100.0, 102.0, 98.0, 105.0]::DOUBLE[] AS actual,
    [101.0, 103.0, 99.0, 106.0]::DOUBLE[] AS pred,
    [100.0, 100.0, 100.0, 100.0]::DOUBLE[] AS naive_baseline
UNION ALL
SELECT 'SeasonalNaive', [100.0, 102.0, 98.0, 105.0]::DOUBLE[], [102.0, 104.0, 100.0, 107.0]::DOUBLE[], [100.0, 100.0, 100.0, 100.0]::DOUBLE[]
UNION ALL
SELECT 'Theta', [100.0, 102.0, 98.0, 105.0]::DOUBLE[], [101.5, 103.5, 99.5, 106.5]::DOUBLE[], [100.0, 100.0, 100.0, 100.0]::DOUBLE[];

-- MASE is scale-independent and baseline-aware
SELECT 
    model_name,
    TS_MASE(actual, pred, naive_baseline) AS mase
FROM model_comparison
ORDER BY mase;

-- Models with MASE < 1.0 beat the baseline
