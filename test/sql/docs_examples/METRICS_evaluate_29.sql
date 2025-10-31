SELECT 
    TS_MAE(actual, predicted) AS mae,      -- Absolute error
    TS_RMSE(actual, predicted) AS rmse,    -- Outlier sensitivity
    TS_MAPE(actual, predicted) AS mape,    -- Percentage error
    TS_R2(actual, predicted) AS r2         -- Variance explained
FROM evaluation;

-- If MAE low but MAPE high → errors concentrated on small values
-- If RMSE >> MAE → outliers present
-- If R² high but MAE high → good trend, poor magnitude
