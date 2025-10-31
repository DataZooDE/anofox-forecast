-- Single seasonality (yearly)
SELECT TS_FORECAST(date, value, 'MFLES', 12, 
       MAP{'seasonal_periods': [12]}) AS forecast
FROM monthly_data;

-- Multiple seasonality (weekly + yearly in daily data)
SELECT TS_FORECAST(date, value, 'MFLES', 30, 
       MAP{'seasonal_periods': [7, 365]}) AS forecast
FROM daily_data;

-- Hourly data (daily + weekly patterns)
SELECT TS_FORECAST(timestamp, value, 'MFLES', 48, 
       MAP{'seasonal_periods': [24, 168]}) AS forecast
FROM hourly_data;

-- Custom learning rates for fine-tuning
SELECT TS_FORECAST(date, value, 'MFLES', 12, 
       MAP{'seasonal_periods': [12], 'n_iterations': 15,
           'lr_trend': 0.4, 'lr_season': 0.6, 'lr_level': 0.9}) AS forecast
FROM data;
