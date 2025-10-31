-- Basic TBATS
SELECT TS_FORECAST(date, value, 'TBATS', 12, 
       MAP{'seasonal_periods': [12]}) AS forecast
FROM monthly_data;

-- TBATS with Box-Cox transformation
SELECT TS_FORECAST(date, value, 'TBATS', 12, 
       MAP{'seasonal_periods': [12], 'use_box_cox': 1, 'box_cox_lambda': 0.5}) AS forecast
FROM non_linear_data;

-- Multiple seasonality with damping
SELECT TS_FORECAST(date, value, 'TBATS', 30, 
       MAP{'seasonal_periods': [7, 365], 'use_damped_trend': 1, 
           'damping_param': 0.95}) AS forecast
FROM daily_data;
