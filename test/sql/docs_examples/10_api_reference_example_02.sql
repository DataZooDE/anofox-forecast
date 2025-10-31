SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, 
                          {'seasonal_period': 7, 'confidence_level': 0.95});
