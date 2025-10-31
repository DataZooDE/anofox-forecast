TS_FORECAST(
    table_name: VARCHAR,
    date_col: DATE | TIMESTAMP,
    value_col: DOUBLE,
    method: VARCHAR,
    horizon: INT,
    params: MAP<VARCHAR, ANY>
) → TABLE
