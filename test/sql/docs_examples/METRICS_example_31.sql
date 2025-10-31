-- MASE is scale-independent and baseline-aware
SELECT 
    model_name,
    TS_MASE(actual, pred, naive_baseline) AS mase
FROM model_comparison
ORDER BY mase;

-- Models with MASE < 1.0 beat the baseline
