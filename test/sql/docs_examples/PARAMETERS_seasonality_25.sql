-- Single seasonality
SELECT TS_FORECAST(date, value, 'MSTL', 12, 
       MAP{'seasonal_periods': [12]}) AS forecast
FROM monthly_data;

-- Multiple seasonality
SELECT TS_FORECAST(date, value, 'MSTL', 30, 
       MAP{'seasonal_periods': [7, 365], 'trend_method': 0}) AS forecast
FROM daily_data;
