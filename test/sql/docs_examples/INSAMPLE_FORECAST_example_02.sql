SELECT 
    confidence_level,
    ROUND((upper - lower), 2) AS interval_width
FROM TS_FORECAST('sales', date, amount, 'ETS', 28, 
                 {'confidence_level': 0.95, 'seasonal_period': 7});
-- confidence_level = 0.95
