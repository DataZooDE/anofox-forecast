-- Create sample raw sensor data
CREATE TABLE sensor_raw AS
SELECT 
    sensor_id,
    TIMESTAMP '2024-01-01 00:00:00' + INTERVAL (h) HOUR AS timestamp,
    CASE 
        WHEN RANDOM() < 0.05 THEN NULL
        ELSE 20 + sensor_id * 2 + 5 * SIN(2 * PI() * h / 24) + (RANDOM() * 2)
    END AS temperature
FROM generate_series(0, 167) t(h)
CROSS JOIN (VALUES (1), (2), (3)) sensors(sensor_id);

-- IoT sensor data with measurement errors
CREATE TABLE sensor_prepared AS
WITH 
-- Remove extreme outliers (sensor malfunction)
outliers_removed AS (
    SELECT 
        sensor_id,
        timestamp,
        CASE 
            WHEN temperature > 1000 OR temperature < -50 THEN NULL  -- Physically impossible
            ELSE temperature
        END AS measurement
    FROM sensor_raw
),
-- Interpolate gaps (linear)
interpolated AS (
    SELECT 
        sensor_id,
        timestamp,
        COALESCE(
            measurement,
            -- Linear interpolation between neighbors
            (LAST_VALUE(measurement IGNORE NULLS) 
                OVER (PARTITION BY sensor_id ORDER BY timestamp 
                      ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW) +
             FIRST_VALUE(measurement IGNORE NULLS) 
                OVER (PARTITION BY sensor_id ORDER BY timestamp 
                      ROWS BETWEEN CURRENT ROW AND UNBOUNDED FOLLOWING)) / 2.0
        ) AS measurement
    FROM outliers_removed
)
SELECT * FROM interpolated;
