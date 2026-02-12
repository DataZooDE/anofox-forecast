# Feature Extraction Reference

Extract 117 tsfresh-compatible features from time series data.

## ts_features_by

```sql
ts_features_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN) → TABLE
```

Returns: group_col (original name preserved) + 116 feature columns including mean, standard_deviation, skewness, kurtosis, length, linear_trend_slope, autocorrelation_lag1, etc.

Example:
```sql
-- Extract all features
SELECT * FROM ts_features_by('sales', product_id, date, quantity);

-- Filter by specific features
SELECT product_id, mean, linear_trend_slope
FROM ts_features_by('sales', product_id, date, quantity)
WHERE length > 30;
```

## ts_features_list

List available feature metadata.

```sql
ts_features_list() → TABLE(column_name VARCHAR, feature_name VARCHAR, parameter_suffix VARCHAR, default_parameters VARCHAR, parameter_keys VARCHAR)
```

Example:
```sql
SELECT feature_name FROM ts_features_list();
```

## Configuration Functions

### ts_features_config_template
```sql
ts_features_config_template() → STRUCT
```
Returns template configuration with all features and defaults.

### ts_features_config_from_csv
```sql
ts_features_config_from_csv(path VARCHAR) → STRUCT(feature_names VARCHAR[], overrides VARCHAR[])
```

CSV format:
```csv
feature_name,enabled,custom_param
mean,true,
autocorrelation,true,lag=5
```

### ts_features_config_from_json
```sql
ts_features_config_from_json(path VARCHAR) → STRUCT(feature_names VARCHAR[], overrides STRUCT(feature VARCHAR, params_json VARCHAR)[])
```

JSON format:
```json
{
  "features": ["mean", "variance", "skewness"],
  "parameters": {"autocorrelation": {"lag": 10}}
}
```
