-- Forecast demand to determine capacity needs
WITH demand_forecast AS (
    SELECT * FROM TS_FORECAST('daily_orders', date, order_count, 'AutoETS', 30,
                              {'seasonal_period': 7, 'confidence_level': 0.95})
),
capacity_requirement AS (
    SELECT 
        date_col AS date,
        CEIL(upper / 100.0) AS staff_required,  -- Each staff handles 100 orders
        CEIL(point_forecast / 100.0) AS expected_staff,
        CEIL(lower / 100.0) AS minimum_staff
    FROM demand_forecast
)
SELECT 
    date,
    expected_staff,
    staff_required AS staff_for_95ci,
    staff_required - expected_staff AS safety_buffer
FROM capacity_requirement;
