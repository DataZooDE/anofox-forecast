-- SeasonalNaive without seasonal_period
SELECT TS_FORECAST(value, 'SeasonalNaive', 5, NULL);
-- Expected: Error about missing seasonal_period

-- HoltWinters without seasonal_period
SELECT TS_FORECAST(value, 'HoltWinters', 5, NULL);
-- Expected: Error about missing seasonal_period
