SELECT 
    TS_MAE(LIST(actual), LIST(forecast)) AS mae,
    TS_RMSE(LIST(actual), LIST(forecast)) AS rmse,
    TS_MAPE(LIST(actual), LIST(forecast)) AS mape,
    TS_SMAPE(LIST(actual), LIST(forecast)) AS smape
FROM results;
