SELECT TS_FORECAST(date, value, 'AutoTBATS', 30, 
       MAP{'seasonal_periods': [7, 365]}) AS forecast
FROM complex_data;
