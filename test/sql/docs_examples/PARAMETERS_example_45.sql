-- ERROR: window must be positive
SELECT TS_FORECAST(date, value, 'SMA', 7, MAP{'window': 0});

-- ERROR: seasonal_period is required
SELECT TS_FORECAST(date, value, 'SeasonalNaive', 7, MAP{});

-- ERROR: alpha must be between 0 and 1
SELECT TS_FORECAST(date, value, 'SES', 7, MAP{'alpha': 1.5});

-- ERROR: Unknown model
SELECT TS_FORECAST(date, value, 'InvalidModel', 7, MAP{});

-- ERROR: seasonal_periods must be array
SELECT TS_FORECAST(date, value, 'MSTL', 7, MAP{'seasonal_periods': 12});

-- CORRECT: seasonal_periods as array
SELECT TS_FORECAST(date, value, 'MSTL', 7, MAP{'seasonal_periods': [12]});
