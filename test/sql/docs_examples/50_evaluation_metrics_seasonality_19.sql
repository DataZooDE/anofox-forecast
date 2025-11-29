-- Over-forecasting
SELECT anofox_fcst_ts_bias([100, 102, 105], [103, 105, 108]) AS bias;
-- Result: +3.0 → Systematically over-forecasting by 3 units

-- Under-forecasting
SELECT anofox_fcst_ts_bias([100, 102, 105], [98, 100, 103]) AS bias;
-- Result: -2.0 → Systematically under-forecasting by 2 units

-- Unbiased
SELECT anofox_fcst_ts_bias([100, 102, 105], [101, 101, 106]) AS bias;
-- Result: 0.0 → Errors cancel out (no systematic bias)
