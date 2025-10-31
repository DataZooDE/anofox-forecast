-- Additive seasonality
SELECT TS_FORECAST(date, sales, 'HoltWinters', 12, 
       MAP{'seasonal_period': 12, 'multiplicative': 0}) AS forecast
FROM monthly_sales;

-- Multiplicative seasonality (for percentage-based seasonality)
SELECT TS_FORECAST(date, sales, 'HoltWinters', 12, 
       MAP{'seasonal_period': 12, 'multiplicative': 1}) AS forecast
FROM retail_sales;
