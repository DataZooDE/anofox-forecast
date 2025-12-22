# Data Directory

This directory contains feature configuration files and sample data for the anofox-forecast extension.

## Feature Configuration Files

### `all_features_overrides.json` and `all_features_overrides.csv`

These files contain the complete catalog of all available time series features (76+ features) with their default parameter configurations. Both files contain the same information in different formats:

- **JSON format** (`all_features_overrides.json`): Structured format suitable for programmatic use with `ts_features_config_from_json()`
- **CSV format** (`all_features_overrides.csv`): Tabular format for easy inspection and editing

These files serve as the single source of truth for which tsfresh-compatible features are available in the extension and how they are parameterized.

**Usage Example:**
```sql
SELECT ts_features(
    ts,
    value,
    ts_features_config_from_json('data/all_features_overrides.json')
) AS feats
FROM time_series_data;
```

### `catch22_feature_overrides.json`

This file contains the subset of [catch22 features](https://github.com/DynamicsAndNeuralSystems/catch22) that are currently available in the codebase. 

**Important Note:** Only **14 out of 22** catch22 features are currently implemented. The catch22 feature set is a curated collection of 22 interpretable time series features selected from the hctsa (Hundreds of Competing Time Series Analysis) library. While many catch22 features have direct or approximate equivalents in tsfresh, some specialized features (such as DN_HistogramMode_5, CO_HistogramAMI_even_2_5, SC_FluctAnal_2_dfa_50_1_2_logi_prop_r1, etc.) are not yet available in the current implementation.

**Available catch22 features (14):**
- `autocorrelation` (lag=1) - Maps to CO_f1ecac and CO_FirstMin_ac
- `fft_aggregated` (centroid) - Maps to SP_Summaries_welch_rect_centroid
- `first_location_of_maximum` - Location-based feature
- `first_location_of_minimum` - Location-based feature
- `kurtosis` - Distributional feature
- `linear_trend` (stderr) - Maps to FC_LocalSimple_mean3_stderr
- `longest_strike_above_mean` - Maps to SB_BinaryStats_mean_longstretch1
- `mean` - Basic statistical feature
- `ratio_beyond_r_sigma` (r=3.0) - Maps to DN_OutlierInclude_p_001_mdrmd and DN_OutlierInclude_n_001_mdrmd
- `skewness` - Distributional feature
- `spkt_welch_density` (coeff=2) - Maps to SP_Summaries_welch_rect_area_5_1
- `standard_deviation` - Basic statistical feature
- `time_reversal_asymmetry_statistic` (lag=1) - Maps to CO_trev_1_num
- `variance` - Basic statistical feature

**Usage Example:**
```sql
SELECT ts_features(
    ts,
    value,
    ts_features_config_from_json('data/catch22_feature_overrides.json')
) AS feats
FROM time_series_data;
```
