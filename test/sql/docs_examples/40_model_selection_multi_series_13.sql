-- Recommended: AutoMSTL (handles multiple seasonality)
SELECT * FROM TS_FORECAST('hourly_traffic', timestamp, visitors,
                          'AutoMSTL', 168,  -- 1 week ahead
                          {'seasonal_periods': [24, 168]});

-- Alternative: TBATS (more complex but slower)
SELECT * FROM TS_FORECAST('hourly_traffic', timestamp, visitors,
                          'AutoTBATS', 168,
                          {'seasonal_periods': [24, 168]});
