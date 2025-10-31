-- Determine delivery fleet requirements
WITH delivery_forecast AS (
    SELECT * FROM TS_FORECAST('daily_deliveries', date, delivery_count,
                              'AutoETS', 30, {'seasonal_period': 7})
),
fleet_needs AS (
    SELECT 
        date_col AS date,
        EXTRACT(DOW FROM date_col) AS day_of_week,
        upper AS deliveries_95ci,
        -- Assume: 1 truck = 40 deliveries/day
        CEIL(upper / 40.0) AS trucks_needed
    FROM delivery_forecast
)
SELECT 
    day_of_week,
    ROUND(AVG(deliveries_95ci), 0) AS avg_deliveries,
    MAX(trucks_needed) AS peak_trucks_needed,
    AVG(trucks_needed) AS avg_trucks_needed,
    MAX(trucks_needed) - AVG(trucks_needed) AS flex_capacity_needed
FROM fleet_needs
WHERE date >= CURRENT_DATE AND date < CURRENT_DATE + INTERVAL '30 days'
GROUP BY day_of_week
ORDER BY day_of_week;
