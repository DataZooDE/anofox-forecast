-- Create sample data
CREATE TABLE sales (date TIMESTAMP, amount DOUBLE);
INSERT INTO sales VALUES 
    ('2024-01-01', 100), ('2024-01-02', 105), ('2024-01-03', 110),
    ('2024-01-04', 108), ('2024-01-05', 112), ('2024-01-06', 115),
    ('2024-01-07', 118), ('2024-01-08', 120), ('2024-01-09', 122),
    ('2024-01-10', 125);

-- Generate 5-step forecast using Naive method
SELECT 
    forecast_step,
    point_forecast,
    lower_95,
    upper_95,
    model_name
FROM FORECAST('date', 'amount', 'Naive', 5, NULL)
ORDER BY forecast_step;

-- Result:
-- forecast_step | point_forecast | lower_95 | upper_95 | model_name
-- 1            | 125.0         | 112.5    | 137.5    | Naive
-- 2            | 125.0         | 112.5    | 137.5    | Naive
-- ...
