-- Weekly seasonality (7 days)
SELECT TS_FORECAST(date, value, 'SeasonalNaive', 14, MAP{'seasonal_period': 7}) AS forecast
FROM daily_data
GROUP BY product_id;

-- Monthly seasonality (12 months)
SELECT TS_FORECAST(month, value, 'SeasonalNaive', 6, MAP{'seasonal_period': 12}) AS forecast
FROM monthly_data;

-- Hourly data with daily seasonality (24 hours)
SELECT TS_FORECAST(hour, value, 'SeasonalNaive', 48, MAP{'seasonal_period': 24}) AS forecast
FROM hourly_data;
