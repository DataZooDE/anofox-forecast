SELECT MAX(date_col) AS last_change
FROM TS_DETECT_CHANGEPOINTS('data', date, value, MAP{})
WHERE is_changepoint = true;
