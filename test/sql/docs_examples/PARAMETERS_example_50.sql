-- Hourly visitors with daily pattern
SELECT 
    TS_FORECAST(timestamp, visitors, 'AutoETS', 48, MAP{'season_length': 24}) AS forecast
FROM web_analytics;
