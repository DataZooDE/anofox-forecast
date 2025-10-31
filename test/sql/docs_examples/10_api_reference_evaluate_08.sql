SELECT 
    TS_MAE(LIST(actual), LIST(forecast)) AS mae,
    TS_RMSE(LIST(actual), LIST(forecast)) AS rmse,
    TS_MAPE(LIST(actual), LIST(forecast)) AS mape,
    TS_R2(LIST(actual), LIST(forecast)) AS r_squared,
    TS_COVERAGE(LIST(actual), LIST(lower), LIST(upper)) AS coverage
FROM results;
