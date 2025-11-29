-- Compare Theta against Naive baseline
SELECT anofox_fcst_ts_mase(
    [100, 102, 105, 103, 107],  -- actual
    [101, 101, 104, 104, 106],  -- theta predictions
    [100, 100, 100, 100, 100]   -- naive baseline
) AS mase;
-- Result: 0.24 → Theta is 76% better than Naive ✅
