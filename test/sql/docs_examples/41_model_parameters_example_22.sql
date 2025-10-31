-- Full automatic selection
SELECT TS_FORECAST(date, value, 'AutoETS', 12, 
       MAP{'season_length': 1}) AS forecast
FROM data;

-- Force additive trend, auto error and season
SELECT TS_FORECAST(date, value, 'AutoETS', 12, 
       MAP{'season_length': 12, 'model': 'ZAZ'}) AS forecast
FROM seasonal_data;

-- Force specific model: ETS(M,N,M)
SELECT TS_FORECAST(date, value, 'AutoETS', 12, 
       MAP{'season_length': 12, 'model': 'MNM'}) AS forecast
FROM multiplicative_data;

-- Weekly seasonality, auto selection
SELECT TS_FORECAST(date, value, 'AutoETS', 14, 
       MAP{'season_length': 7, 'model': 'ZZZ'}) AS forecast
FROM daily_data;
