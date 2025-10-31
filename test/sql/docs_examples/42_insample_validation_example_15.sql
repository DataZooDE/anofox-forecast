TS_FORECAST(table, date_col, value_col, method, horizon, params)

-- params:
{
    'return_insample': BOOLEAN,      -- Default: false
    'confidence_level': DOUBLE,      -- Default: 0.90
    ... other model-specific params
}
