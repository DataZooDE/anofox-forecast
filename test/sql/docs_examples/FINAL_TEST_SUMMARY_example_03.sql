-- Create composite key
CREATE TABLE data_with_composite AS
SELECT 
    region || '|' || store || '|' || product AS composite_id,
    date,
    sales,
    region, store, product  -- Keep originals
FROM raw_data;

-- Use ts_forecast_by with composite
SELECT * FROM ts_forecast_by(
    data_with_composite,
    composite_id,
    date,
    sales,
    'Naive',
    7,
    NULL
);

-- Or use manual GROUP BY
SELECT 
    region, store, product,
    TS_FORECAST_AGG(date, sales, 'Naive', 7, NULL) AS result
FROM raw_data
GROUP BY region, store, product;

-- Split composite ID back
SELECT 
    SPLIT_PART(composite_id, '|', 1) AS region,
    SPLIT_PART(composite_id, '|', 2) AS store,
    SPLIT_PART(composite_id, '|', 3) AS product,
    *
FROM forecasts;
