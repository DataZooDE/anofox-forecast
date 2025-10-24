-- AutoETS Bug Test
LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

CREATE OR REPLACE TABLE airp AS
WITH data AS (
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
           360, 342, 406, 396, 420, 472, 548, 559, 463, 407, 362, 405])::DOUBLE AS v,
           UNNEST(generate_series(1, 132)) AS idx
)
SELECT 
    DATE '1949-01-01' + INTERVAL ((idx - 1) * 30) DAY AS date,
    v AS passengers
FROM data;

SELECT 'AutoETS Test' AS info;

WITH forecast AS (
    SELECT TS_FORECAST(date, passengers, 'AutoETS', 12, {'season_length': 12}) AS f 
    FROM airp
)
SELECT 
    UNNEST(generate_series(1, 12)) AS month,
    UNNEST(f.point_forecast) AS autoets_forecast
FROM forecast;

SELECT '
Expected from statsforecast AutoETS:
[407, 402, 456, 441, 440, 497, 546, 545, 477, 412, 358, 402]

Actual from anofox-time:
Near zero (bug!)

This confirms AutoETS has a critical bug.
' AS analysis;

