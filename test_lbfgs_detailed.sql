LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

CREATE OR REPLACE TABLE airp AS
SELECT DATE '1949-01-01' + INTERVAL ((idx - 1) * 30) DAY AS date,
       val AS passengers
FROM (
    SELECT UNNEST([112, 118, 132, 129, 121, 135, 148, 148, 136, 119, 104, 118,
           115, 126, 141, 135, 125, 149, 170, 170, 158, 133, 114, 140,
           145, 150, 178, 163, 172, 178, 199, 199, 184, 162, 146, 166,
           171, 180, 193, 181, 183, 218, 230, 242, 209, 191, 172, 194,
           196, 196, 236, 235, 229, 243, 264, 272, 237, 211, 180, 201,
           204, 188, 235, 227, 234, 264, 302, 293, 259, 229, 203, 229,
           242, 233, 267, 269, 270, 315, 364, 347, 312, 274, 237, 278,
           284, 277, 317, 313, 318, 374, 413, 405, 355, 306, 271, 306,
           315, 301, 356, 348, 355, 422, 465, 467, 404, 347, 305, 336,
           340, 318, 362, 348, 363, 435, 491, 505, 404, 359, 310, 337,
           360, 342, 406, 396, 420, 472, 548, 559, 463, 407, 362, 405])::DOUBLE AS val,
           UNNEST(generate_series(1, 132)) AS idx
);

SELECT '=== Testing MNM with L-BFGS ===' AS info;
WITH forecasts AS (
    SELECT TS_FORECAST(date, passengers, 'AutoETS', 12, {'season_length': 12, 'model': 'MNM'}) AS f
    FROM airp
)
SELECT 
    ROUND(f.point_forecast[1], 2) AS fc_1,
    ROUND(f.point_forecast[2], 2) AS fc_2,
    ROUND(f.point_forecast[3], 2) AS fc_3
FROM forecasts;

SELECT '
Expected (statsforecast):
  MNM: [407, 402, 456]
' AS note;

SELECT '=== Testing MAM with L-BFGS ===' AS info;
WITH forecasts AS (
    SELECT TS_FORECAST(date, passengers, 'AutoETS', 12, {'season_length': 12, 'model': 'MAM'}) AS f
    FROM airp
)
SELECT 
    ROUND(f.point_forecast[1], 2) AS fc_1,
    ROUND(f.point_forecast[2], 2) AS fc_2,
    ROUND(f.point_forecast[3], 2) AS fc_3
FROM forecasts;

SELECT '
Expected (statsforecast):
  MAM: [412, 409, 474]
' AS note;

