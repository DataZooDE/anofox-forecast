-- Test AutoARIMA with AirPassengers Dataset
-- Expected results from statsforecast Python package for validation
LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- Create AirPassengers dataset (1949-1960, monthly)
CREATE OR REPLACE TABLE airpassengers AS
WITH data(idx, value) AS (
    VALUES 
    (1, 112), (2, 118), (3, 132), (4, 129), (5, 121), (6, 135), (7, 148), (8, 148), (9, 136), (10, 119), (11, 104), (12, 118),
    (13, 115), (14, 126), (15, 141), (16, 135), (17, 125), (18, 149), (19, 170), (20, 170), (21, 158), (22, 133), (23, 114), (24, 140),
    (25, 145), (26, 150), (27, 178), (28, 163), (29, 172), (30, 178), (31, 199), (32, 199), (33, 184), (34, 162), (35, 146), (36, 166),
    (37, 171), (38, 180), (39, 193), (40, 181), (41, 183), (42, 218), (43, 230), (44, 242), (45, 209), (46, 191), (47, 172), (48, 194),
    (49, 196), (50, 196), (51, 236), (52, 235), (53, 229), (54, 243), (55, 264), (56, 272), (57, 237), (58, 211), (59, 180), (60, 201),
    (61, 204), (62, 188), (63, 235), (64, 227), (65, 234), (66, 264), (67, 302), (68, 293), (69, 259), (70, 229), (71, 203), (72, 229),
    (73, 242), (74, 233), (75, 267), (76, 269), (77, 270), (78, 315), (79, 364), (80, 347), (81, 312), (82, 274), (83, 237), (84, 278),
    (85, 284), (86, 277), (87, 317), (88, 313), (89, 318), (90, 374), (91, 413), (92, 405), (93, 355), (94, 306), (95, 271), (96, 306),
    (97, 315), (98, 301), (99, 356), (100, 348), (101, 355), (102, 422), (103, 465), (104, 467), (105, 404), (106, 347), (107, 305), (108, 336),
    (109, 340), (110, 318), (111, 362), (112, 348), (113, 363), (114, 435), (115, 491), (116, 505), (117, 404), (118, 359), (119, 310), (120, 337),
    (121, 360), (122, 342), (123, 406), (124, 396), (125, 420), (126, 472), (127, 548), (128, 559), (129, 463), (130, 407), (131, 362), (132, 405),
    -- Last 12 values for validation (comparing our forecast to these actuals)
    (133, 417), (134, 391), (135, 419), (136, 461), (137, 472), (138, 535), (139, 622), (140, 606), (141, 508), (142, 461), (143, 390), (144, 432)
)
SELECT 
    DATE '1949-01-01' + INTERVAL ((idx - 1) * 30) DAY AS date,
    idx,
    value::DOUBLE AS passengers
FROM data;

SELECT 'AirPassengers Dataset (1949-1960, Monthly):' AS info;
SELECT COUNT(*) AS total_months, MIN(passengers) AS min_passengers, MAX(passengers) AS max_passengers,
       ROUND(AVG(passengers), 1) AS avg_passengers
FROM airpassengers;

-- Test 1: Use first 132 observations, forecast 12 steps (like statsforecast test)
CREATE OR REPLACE TABLE airpassengers_train AS
SELECT * FROM airpassengers WHERE idx <= 132;

SELECT '
Test 1: AutoARIMA on AirPassengers (training on first 132 months, forecast 12)' AS test;

WITH forecast_result AS (
    SELECT TS_FORECAST(date, passengers, 'AutoARIMA', 12, {'seasonal_period': 12}) AS forecast
    FROM airpassengers_train
)
SELECT 
    UNNEST(forecast.forecast_step) AS month,
    ROUND(UNNEST(forecast.point_forecast), 2) AS autoarima_forecast
FROM forecast_result;

SELECT '
Expected from statsforecast (for comparison):
Month 1: 424.08
Month 2: 407.04
Month 3: 470.81
Month 4: 460.87
Month 5: 484.85
Month 6: 536.85
Month 7: 612.85
Month 8: 623.85
Month 9: 527.85
Month 10: 471.85
Month 11: 426.85
Month 12: 469.85
' AS expected_values;

SELECT '
Actual values (months 133-144 from dataset):
Month 1: 417
Month 2: 391
Month 3: 419
Month 4: 461
Month 5: 472
Month 6: 535
Month 7: 622
Month 8: 606
Month 9: 508
Month 10: 461
Month 11: 390
Month 12: 432
' AS actual_values;

-- Test 2: Compare with simpler models
SELECT '
Test 2: Compare AutoARIMA with other models' AS test;

WITH forecasts AS (
    SELECT 'Naive' AS model, TS_FORECAST(date, passengers, 'Naive', 12, NULL) AS f 
    FROM airpassengers_train
    UNION ALL SELECT 'SeasonalNaive', TS_FORECAST(date, passengers, 'SeasonalNaive', 12, {'seasonal_period': 12})
    FROM airpassengers_train
    UNION ALL SELECT 'Theta', TS_FORECAST(date, passengers, 'Theta', 12, {'seasonal_period': 12})
    FROM airpassengers_train
    UNION ALL SELECT 'AutoETS', TS_FORECAST(date, passengers, 'AutoETS', 12, {'season_length': 12})
    FROM airpassengers_train
    UNION ALL SELECT 'AutoARIMA', TS_FORECAST(date, passengers, 'AutoARIMA', 12, {'seasonal_period': 12})
    FROM airpassengers_train
)
SELECT 
    model,
    ROUND(f.point_forecast[1], 1) AS jan,
    ROUND(f.point_forecast[6], 1) AS jun,
    ROUND(f.point_forecast[12], 1) AS dec,
    ROUND(AVG(UNNEST(f.point_forecast)), 1) AS avg_forecast
FROM forecasts
ORDER BY model;

SELECT '
Analysis: If AutoARIMA forecast is dramatically higher than other models,
there is likely a bug in the AutoARIMA implementation.

Expected AutoARIMA forecast average: ~480 (similar to other models)
If seeing ~2000+: BUG CONFIRMED
' AS analysis;

