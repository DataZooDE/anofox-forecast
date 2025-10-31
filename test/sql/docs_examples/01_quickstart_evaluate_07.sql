-- If you have actual future values
SELECT 
    TS_MAE(LIST(actual), LIST(forecast)) AS mae,
    TS_RMSE(LIST(actual), LIST(forecast)) AS rmse
FROM results;
