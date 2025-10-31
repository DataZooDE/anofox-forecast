SELECT 
    COUNT(*) FILTER (WHERE is_changepoint) AS total_changepoints
FROM TS_DETECT_CHANGEPOINTS('data', date, value, MAP{});
