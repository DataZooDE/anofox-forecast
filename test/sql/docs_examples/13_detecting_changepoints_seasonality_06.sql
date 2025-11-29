-- Create sample sensor data
CREATE TABLE sensor_data AS
SELECT 
    TIMESTAMP '2024-01-01 00:00:00' + INTERVAL (h) HOUR AS timestamp,
    20 + 5 * SIN(2 * PI() * h / 24) + (RANDOM() * 2) AS temperature
FROM generate_series(0, 167) t(h);  -- 7 days

-- Find anomalous periods marked by changepoints
SELECT 
    date_col,
    value_col,
    'ANOMALY DETECTED' AS alert
FROM anofox_fcst_ts_detect_changepoints('sensor_data', timestamp, temperature, MAP{})
WHERE is_changepoint = true
  AND date_col >= CURRENT_DATE - INTERVAL '7 days'
ORDER BY date_col;
