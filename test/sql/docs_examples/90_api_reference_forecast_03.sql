TS_FORECAST_BY(
    table_name: VARCHAR,
    group_col: VARCHAR,
    date_col: DATE | TIMESTAMP,
    value_col: DOUBLE,
    method: VARCHAR,
    horizon: INT,
    params: MAP<VARCHAR, ANY>
) → TABLE
