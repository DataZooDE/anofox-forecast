-- IoT sensor data with measurement errors
CREATE TABLE sensor_prepared AS
WITH 
-- Remove extreme outliers (sensor malfunction)
outliers_removed AS (
    SELECT 
        sensor_id,
        timestamp,
        CASE 
            WHEN measurement > 1000 OR measurement < -50 THEN NULL  -- Physically impossible
            ELSE measurement
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
