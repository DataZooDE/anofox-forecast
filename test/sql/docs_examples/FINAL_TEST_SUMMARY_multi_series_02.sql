-- Single series
ts_forecast(table_name, date_col, value_col, model, horizon, params)

-- Grouped (1 ID column)
ts_forecast_by(table_name, group_col, date_col, value_col, model, horizon, params)

-- Multiple ID columns (use manual GROUP BY)
SELECT id1, id2, id3, TS_FORECAST_AGG(date_col, value_col, model, horizon, params)
FROM table
GROUP BY id1, id2, id3;
