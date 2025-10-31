-- ETS(A,N,N) - Simple exponential smoothing
SELECT TS_FORECAST(date, value, 'ETS', 12, 
       MAP{'error_type': 0, 'trend_type': 0, 'season_type': 0}) AS forecast
FROM level_data;

-- ETS(A,A,N) - Holt's linear trend
SELECT TS_FORECAST(date, value, 'ETS', 12, 
       MAP{'error_type': 0, 'trend_type': 1, 'season_type': 0}) AS forecast
FROM trending_data;

-- ETS(A,Ad,A) - Damped trend with additive seasonality
SELECT TS_FORECAST(date, value, 'ETS', 12, 
       MAP{'error_type': 0, 'trend_type': 2, 'season_type': 1, 
           'season_length': 12, 'phi': 0.9}) AS forecast
FROM seasonal_damped_data;

-- ETS(M,N,M) - Multiplicative error and seasonality
SELECT TS_FORECAST(date, value, 'ETS', 12, 
       MAP{'error_type': 1, 'trend_type': 0, 'season_type': 2, 
           'season_length': 12}) AS forecast
FROM percentage_seasonal_data;
