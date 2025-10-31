-- Recommended: TBATS or MSTL
SELECT * FROM TS_FORECAST('electricity_demand', timestamp, kwh,
                          'AutoMSTL', 336,  -- 1 week
                          {'seasonal_periods': [48, 336]});
