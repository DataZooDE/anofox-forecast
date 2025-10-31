-- Find anomalous periods marked by changepoints
SELECT 
    date_col,
    value_col,
    'ANOMALY DETECTED' AS alert
FROM TS_DETECT_CHANGEPOINTS('sensor_data', timestamp, temperature, MAP{})
WHERE is_changepoint = true
  AND date_col >= CURRENT_DATE - INTERVAL '7 days'
ORDER BY date_col;
