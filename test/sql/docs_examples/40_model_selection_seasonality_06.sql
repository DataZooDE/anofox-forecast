-- Simple and effective
SELECT * FROM TS_FORECAST('sales', date, amount, 'OptimizedTheta', 28, {'seasonal_period': 7});
