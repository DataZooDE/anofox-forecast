# Time Series Evaluation Metrics

This document describes the evaluation metric functions available in the DuckDB Anofox Forecast Extension.

---

## Table of Contents

1. [Example: Forecasting and Evaluation with Multiple Methods](#example-forecasting-and-evaluation-with-multiple-methods)
2. [Using Metrics with GROUP BY](#using-metrics-with-group-by)
3. [Function Signatures](#function-signatures)
   - [TS_MAE - Mean Absolute Error](#ts_mae---mean-absolute-error)
   - [TS_MSE - Mean Squared Error](#ts_mse---mean-squared-error)
   - [TS_RMSE - Root Mean Squared Error](#ts_rmse---root-mean-squared-error)
   - [TS_MAPE - Mean Absolute Percentage Error](#ts_mape---mean-absolute-percentage-error)
   - [TS_SMAPE - Symmetric Mean Absolute Percentage Error](#ts_smape---symmetric-mean-absolute-percentage-error)
   - [TS_MASE - Mean Absolute Scaled Error](#ts_mase---mean-absolute-scaled-error)
   - [TS_R2 - Coefficient of Determination](#ts_r2---coefficient-of-determination)
   - [TS_BIAS - Forecast Bias](#ts_bias---forecast-bias)
   - [TS_RMAE - Relative Mean Absolute Error](#ts_rmae---relative-mean-absolute-error)
   - [TS_QUANTILE_LOSS - Quantile Loss](#ts_quantile_loss---quantile-loss-pinball-loss)
   - [TS_MQLOSS - Multi-Quantile Loss](#ts_mqloss---multi-quantile-loss)
   - [TS_COVERAGE - Prediction Interval Coverage](#ts_coverage---prediction-interval-coverage)
4. [Error Handling](#error-handling)

---

## Example: Forecasting and Evaluation with Multiple Methods

The following example demonstrates generating forecasts using three different forecasting methods for multiple time series with GROUP BY, then evaluating the forecasts using `TS_MAE` and `TS_BIAS` metrics.

```sql
-- Generate forecasts using three different methods for each product
CREATE TEMP TABLE forecasts AS
SELECT * FROM TS_FORECAST_BY(
    'sales',
    product_id,
    date,
    revenue,
    'AutoETS',
    28,
    MAP{'seasonal_period': 7}
)
UNION ALL
SELECT * FROM TS_FORECAST_BY(
    'sales',
    product_id,
    date,
    revenue,
    'SeasonalNaive',
    28,
    MAP{'seasonal_period': 7}
)
UNION ALL
SELECT * FROM TS_FORECAST_BY(
    'sales',
    product_id,
    date,
    revenue,
    'AutoARIMA',
    28,
    MAP{'seasonal_period': 7}
);

-- Join forecasts with actual test data
CREATE TEMP TABLE evaluation_data AS
SELECT 
    f.product_id,
    f.model_name,
    f.date,
    f.point_forecast,
    a.actual_value
FROM forecasts f
JOIN test_data a ON f.product_id = a.product_id AND f.date = a.date;

-- Evaluate forecasts using TS_MAE and TS_BIAS per product and model
SELECT 
    product_id,
    model_name,
    TS_MAE(LIST(actual_value), LIST(point_forecast)) AS mae,
    TS_BIAS(LIST(actual_value), LIST(point_forecast)) AS bias
FROM evaluation_data
GROUP BY product_id, model_name
ORDER BY product_id, mae;
```

This example demonstrates:

- Generating forecasts for multiple time series using `TS_FORECAST_BY` with three different methods (AutoETS, SeasonalNaive, AutoARIMA)
- Combining forecasts from multiple methods using `UNION ALL`
- Joining forecast results with actual test data
- Evaluating forecast accuracy using `TS_MAE` and `TS_BIAS` with `GROUP BY` and `LIST()` aggregation
- Results are grouped by both `product_id` and `model_name` to compare method performance across products

---

## Using Metrics with GROUP BY

All metrics work seamlessly with `GROUP BY` operations using DuckDB's `LIST()` aggregate function.

### Pattern

**Metrics expect arrays (LIST), not individual values:**

```sql
-- ❌ WRONG - This won't work (metrics need arrays)
SELECT product_id, TS_MAE(actual, predicted)
FROM results
GROUP BY product_id;

-- ✅ CORRECT - Use LIST() to create arrays
SELECT 
    product_id,
    TS_MAE(LIST(actual), LIST(predicted)) AS mae
FROM results
GROUP BY product_id;
```

---

## Function Signatures

### TS_MAE - Mean Absolute Error

**Signature:**

```sql
TS_MAE(
    actual      DOUBLE[],
    predicted   DOUBLE[]
) → DOUBLE
```

**Formula:** `MAE = Σ|y - ŷ| / n`

**Range:** [0, ∞)

**Description:**

The Mean Absolute Error (MAE) computes the average absolute difference between actual and predicted values. It provides a measure of forecast accuracy in the same units as the original data. MAE treats all errors equally, making it less sensitive to outliers compared to squared error metrics like MSE or RMSE. Lower values indicate better forecast accuracy. MAE is particularly useful when you need an easily interpretable error metric that represents the typical magnitude of forecast errors without being dominated by occasional large errors.

**Example:**

```sql
SELECT TS_MAE([100, 102, 105], [101, 101, 104]) AS mae;
-- Result: 0.67
```

---

### TS_MSE - Mean Squared Error

**Signature:**

```sql
TS_MSE(
    actual      DOUBLE[],
    predicted   DOUBLE[]
) → DOUBLE
```

**Formula:** `MSE = Σ(y - ŷ)² / n`

**Range:** [0, ∞)

**Description:**

The Mean Squared Error (MSE) computes the average of the squared differences between actual and predicted values. By squaring the errors, MSE penalizes large errors more heavily than small errors, making it sensitive to outliers. The units of MSE are the square of the original data units, which can make interpretation less intuitive. MSE is commonly used in optimization problems because it is differentiable and has desirable mathematical properties. Lower values indicate better forecast accuracy. MSE is particularly useful when large forecast errors are particularly costly or undesirable.

**Example:**

```sql
SELECT TS_MSE([100, 102, 105], [101, 101, 104]) AS mse;
-- Result: 0.67
```

---

### TS_RMSE - Root Mean Squared Error

**Signature:**

```sql
TS_RMSE(
    actual      DOUBLE[],
    predicted   DOUBLE[]
) → DOUBLE
```

**Formula:** `RMSE = √(MSE) = √(Σ(y - ŷ)² / n)`

**Range:** [0, ∞)

**Description:**

The Root Mean Squared Error (RMSE) is the square root of the Mean Squared Error, which restores the metric to the same units as the original data. RMSE maintains the property of penalizing large errors more than small errors, but to a lesser degree than MSE. It provides a measure of the standard deviation of forecast errors. Typically, RMSE ≥ MAE, with equality occurring only when all errors have the same magnitude. RMSE is widely used in forecasting because it balances sensitivity to outliers with interpretability in original units. Lower values indicate better forecast accuracy.

**Example:**

```sql
SELECT TS_RMSE([100, 102, 105], [101, 101, 104]) AS rmse;
-- Result: 0.82
```

---

### TS_MAPE - Mean Absolute Percentage Error

**Signature:**

```sql
TS_MAPE(
    actual      DOUBLE[],
    predicted   DOUBLE[]
) → DOUBLE
```

**Formula:** `MAPE = (100/n) × Σ|y - ŷ| / |y|`

**Range:** [0, ∞) (expressed as percentage)

**Description:**

The Mean Absolute Percentage Error (MAPE) expresses forecast errors as a percentage of the actual values, making it a scale-independent metric that facilitates comparison across different time series with different magnitudes. MAPE is calculated by averaging the absolute percentage errors, where each error is normalized by its corresponding actual value. This normalization allows for meaningful comparisons between series with different scales. However, MAPE has limitations: it is undefined when any actual value is zero, and it can be asymmetric, penalizing under-forecasting more than over-forecasting for the same absolute error. MAPE values are typically interpreted as percentages, with lower values indicating better accuracy.

> [!WARNING]
> Returns NULL if any actual value is zero.

**Example:**

```sql
SELECT TS_MAPE([100, 102, 105], [101, 101, 104]) AS mape_percent;
-- Result: 0.65 (0.65% error)
```

---

### TS_SMAPE - Symmetric Mean Absolute Percentage Error

**Signature:**

```sql
TS_SMAPE(
    actual      DOUBLE[],
    predicted   DOUBLE[]
) → DOUBLE
```

**Formula:** `SMAPE = (200/n) × Σ|y - ŷ| / (|y| + |ŷ|)`

**Range:** [0, 200]

**Description:**

The Symmetric Mean Absolute Percentage Error (SMAPE) is a symmetric version of MAPE that treats over-forecasting and under-forecasting equally. Unlike MAPE, which normalizes errors only by actual values, SMAPE normalizes by the average of actual and predicted values, creating a symmetric penalty structure. This symmetry makes SMAPE more balanced for comparing forecast accuracy. SMAPE also handles zero values better than MAPE, though it can still be problematic when both actual and predicted values are zero. The metric ranges from 0 to 200, with lower values indicating better accuracy. SMAPE is particularly useful when you need a scale-independent metric that treats positive and negative errors symmetrically.

> [!WARNING]
> Handles zero values better than MAPE.

**Example:**

```sql
SELECT TS_SMAPE([100, 102, 105], [101, 101, 104]) AS smape_percent;
-- Result: 0.65 (0.65% error)
```

---

### TS_MASE - Mean Absolute Scaled Error

**Signature:**

```sql
TS_MASE(
    actual      DOUBLE[],
    predicted   DOUBLE[],
    baseline    DOUBLE[]
) → DOUBLE
```

**Formula:** `MASE = MAE(predicted) / MAE(baseline)`

**Range:** [0, ∞)

**Description:**

The Mean Absolute Scaled Error (MASE) measures forecast accuracy relative to a baseline forecasting method, typically a naive forecast. MASE is calculated as the ratio of the MAE of the forecast method being evaluated to the MAE of the baseline method. This scaling makes MASE scale-independent and allows for meaningful comparisons across different time series. A MASE value less than 1.0 indicates that the forecast method performs better than the baseline, while a value greater than 1.0 indicates the baseline is superior. MASE is particularly useful for comparing forecast methods across multiple time series with different scales, and it handles zero values in the data better than percentage-based metrics. The baseline is typically a simple naive forecast (e.g., using the last observed value) or a seasonal naive forecast.

**Example:**

```sql
-- Compare Theta against Naive baseline
SELECT TS_MASE(
    [100, 102, 105, 103, 107],  -- actual
    [101, 101, 104, 104, 106],  -- theta predictions
    [100, 100, 100, 100, 100]   -- naive baseline
) AS mase;
-- Result: 0.24 → Theta is 76% better than Naive ✅
```

---

### TS_R2 - Coefficient of Determination

**Signature:**

```sql
TS_R2(
    actual      DOUBLE[],
    predicted   DOUBLE[]
) → DOUBLE
```

**Formula:** `R² = 1 - (SS_res / SS_tot)`

**Range:** (-∞, 1.0]

**Description:**

The R-squared (R²) coefficient of determination measures the proportion of variance in the actual values that is explained by the forecast model. R² is calculated as one minus the ratio of the residual sum of squares to the total sum of squares. A value of 1.0 indicates a perfect fit where all variance is explained by the model. A value of 0.0 indicates that the model performs as well as simply predicting the mean of the actual values. Negative values indicate that the model performs worse than the mean baseline. R² provides a normalized measure of model performance that is independent of the scale of the data, making it useful for comparing models across different time series. Higher values indicate better model fit, with values above 0.7 generally considered good and above 0.9 considered excellent.

**Example:**

```sql
SELECT TS_R2([100, 102, 105, 103, 107], [101, 101, 104, 104, 106]) AS r_squared;
-- Result: 0.88 → Model explains 88% of variance
```

---

### TS_BIAS - Forecast Bias

**Signature:**

```sql
TS_BIAS(
    actual      DOUBLE[],
    predicted   DOUBLE[]
) → DOUBLE
```

**Formula:** `Bias = Σ(ŷ - y) / n`

**Range:** (-∞, ∞)

**Description:**

The Forecast Bias metric measures the average signed error between predicted and actual values, providing an indication of systematic over-forecasting or under-forecasting. Unlike error metrics that use absolute values, bias preserves the sign of errors, allowing detection of systematic tendencies in forecasts. A positive bias indicates systematic over-forecasting (predictions are consistently higher than actuals), while a negative bias indicates systematic under-forecasting (predictions are consistently lower than actuals). A bias of zero indicates an unbiased forecast, which is the ideal case. However, it is important to note that bias can be zero even when forecast errors are large, if positive and negative errors cancel out. Bias is expressed in the same units as the original data. It should always be used in conjunction with other error metrics like MAE or RMSE to get a complete picture of forecast accuracy, as bias alone does not capture the magnitude of errors.

**Interpretation:** Positive = over-forecasting, Negative = under-forecasting

**Example:**

```sql
-- Over-forecasting
SELECT TS_BIAS([100, 102, 105], [103, 105, 108]) AS bias;
-- Result: +3.0 → Systematically over-forecasting by 3 units

-- Under-forecasting
SELECT TS_BIAS([100, 102, 105], [98, 100, 103]) AS bias;
-- Result: -2.0 → Systematically under-forecasting by 2 units

-- Unbiased
SELECT TS_BIAS([100, 102, 105], [101, 101, 106]) AS bias;
-- Result: 0.0 → Errors cancel out (no systematic bias)
```

---

### TS_RMAE - Relative Mean Absolute Error

**Signature:**

```sql
TS_RMAE(
    actual      DOUBLE[],
    pred1        DOUBLE[],
    pred2        DOUBLE[]
) → DOUBLE
```

**Formula:** `RMAE = MAE(actual, pred1) / MAE(actual, pred2)`

**Range:** [0, ∞)

**Description:**

The Relative Mean Absolute Error (RMAE) compares the performance of two forecasting methods by computing the ratio of their respective MAE values. RMAE provides a direct comparison metric that indicates which method performs better relative to the other. When RMAE is less than 1.0, the first method (pred1) has lower MAE and is therefore better than the second method (pred2). When RMAE is greater than 1.0, the second method performs better. When RMAE equals 1.0, both methods perform equally. RMAE is scale-independent and allows for straightforward comparison of two forecasting approaches on the same dataset. This metric is particularly useful for model selection when you want to directly compare two specific forecasting methods.

**Example:**

```sql
-- Compare AutoETS vs Naive forecast
SELECT 
    TS_MAE(actual, forecast_autoets) AS mae_autoets,
    TS_MAE(actual, forecast_naive) AS mae_naive,
    TS_RMAE(actual, forecast_autoets, forecast_naive) AS relative_performance,
    CASE 
        WHEN TS_RMAE(actual, forecast_autoets, forecast_naive) < 1.0
        THEN 'AutoETS is better'
        ELSE 'Naive is better'
    END AS winner
FROM forecast_comparison;

-- Result: 
-- mae_autoets: 2.0, mae_naive: 4.5, relative_performance: 0.44, winner: 'AutoETS is better'
```

---

### TS_QUANTILE_LOSS - Quantile Loss (Pinball Loss)

**Signature:**

```sql
TS_QUANTILE_LOSS(
    actual      DOUBLE[],
    predicted   DOUBLE[],
    q           DOUBLE
) → DOUBLE
```

**Formula:** `QL = Σ max(q × (y - ŷ), (1 - q) × (ŷ - y))`

**Range:** [0, ∞)

**Description:**

The Quantile Loss, also known as Pinball Loss, evaluates the accuracy of quantile forecasts (prediction intervals) rather than point forecasts. The metric uses an asymmetric loss function that penalizes errors differently depending on whether the actual value is above or below the predicted quantile. The parameter `q` represents the quantile level (0 < q < 1). For the median (q = 0.5), the loss function is symmetric, giving equal weight to over-prediction and under-prediction. For lower quantiles (e.g., q = 0.1), the loss function penalizes over-prediction more heavily, which is appropriate for evaluating lower bounds of prediction intervals. For upper quantiles (e.g., q = 0.9), the loss function penalizes under-prediction more heavily, which is appropriate for evaluating upper bounds. Lower values indicate better quantile forecast accuracy. Quantile loss is essential for evaluating probabilistic forecasts and prediction intervals, as it assesses whether the forecasted quantiles are well-calibrated.

**Parameters:**

- `q`: Quantile level (0 < q < 1)

**Example:**

```sql
-- Evaluate 10th, 50th (median), and 90th percentile forecasts
SELECT 
    TS_QUANTILE_LOSS(actual, lower_bound, 0.1) AS ql_lower,
    TS_QUANTILE_LOSS(actual, median_forecast, 0.5) AS ql_median,
    TS_QUANTILE_LOSS(actual, upper_bound, 0.9) AS ql_upper
FROM forecasts;

-- Perfect median forecast (ql_median = 0.0) means predictions exactly match actuals
-- Lower ql_lower means better lower bound prediction
-- Lower ql_upper means better upper bound prediction
```

---

### TS_MQLOSS - Multi-Quantile Loss

**Signature:**

```sql
TS_MQLOSS(
    actual      DOUBLE[],
    quantiles   DOUBLE[][],
    levels      DOUBLE[]
) → DOUBLE
```

**Formula:** `Average of quantile losses across all quantiles`

**Range:** [0, ∞)

**Description:**

The Multi-Quantile Loss (MQLOSS) evaluates the accuracy of full predictive distributions by averaging quantile losses across multiple quantile levels. This metric provides a comprehensive assessment of probabilistic forecasts that include multiple quantiles, such as prediction intervals with both lower and upper bounds. MQLOSS approximates the Continuous Ranked Probability Score (CRPS), which is a widely used metric for evaluating probabilistic forecasts. By averaging quantile losses across different quantile levels, MQLOSS captures how well the forecasted distribution matches the actual distribution of outcomes. Lower values indicate better distribution forecast accuracy. This metric is particularly useful when evaluating forecasts that provide full predictive distributions rather than just point forecasts, as it assesses the calibration and sharpness of the entire forecast distribution.

**Parameters:**

- `quantiles`: Array of quantile forecast arrays
- `levels`: Corresponding quantile levels (e.g., [0.1, 0.5, 0.9])

**Example:**

```sql
-- Evaluate a 5-quantile forecast distribution
WITH distributions AS (
    SELECT
        [100.0, 110.0, 120.0, 130.0, 140.0] AS actual,
        [
            [90.0, 100.0, 110.0, 120.0, 130.0],    -- q=0.1
            [95.0, 105.0, 115.0, 125.0, 135.0],    -- q=0.25
            [100.0, 110.0, 120.0, 130.0, 140.0],   -- q=0.5 (median)
            [105.0, 115.0, 125.0, 135.0, 145.0],   -- q=0.75
            [110.0, 120.0, 130.0, 140.0, 150.0]    -- q=0.9
        ] AS predicted_quantiles,
        [0.1, 0.25, 0.5, 0.75, 0.9] AS quantiles
)
SELECT 
    TS_MQLOSS(actual, predicted_quantiles, quantiles) AS mqloss,
    'Lower is better - measures full distribution accuracy' AS interpretation
FROM distributions;

-- Result: mqloss = 0.9 (good distribution fit)
```

**Use Case - CRPS Approximation**:

```sql
-- Use many quantiles for better CRPS approximation
WITH dense_quantiles AS (
    SELECT
        actual,
        [q01, q02, q03, ... q99] AS predicted_quantiles,  -- 99 quantiles
        [0.01, 0.02, 0.03, ... 0.99] AS quantiles
    FROM forecast_distributions
)
SELECT 
    model_name,
    TS_MQLOSS(actual, predicted_quantiles, quantiles) AS crps_approximation
FROM dense_quantiles
GROUP BY model_name
ORDER BY crps_approximation;  -- Best model first
```

---

### TS_COVERAGE - Prediction Interval Coverage

**Signature:**

```sql
TS_COVERAGE(
    actual      DOUBLE[],
    lower       DOUBLE[],
    upper       DOUBLE[]
) → DOUBLE
```

**Formula:** `Coverage = (Count of actuals within [lower, upper]) / n`

**Range:** [0, 1]

**Description:**

The Prediction Interval Coverage metric measures the fraction of actual values that fall within the forecasted prediction interval bounds. Coverage is calculated as the proportion of observations where the actual value lies between the lower and upper bounds of the prediction interval. This metric is essential for evaluating the calibration of prediction intervals, as it indicates whether the intervals are appropriately sized. Ideally, the coverage should match the confidence level used to generate the intervals. For example, if prediction intervals were generated with a 90% confidence level, the coverage should be approximately 0.90 (90%). Coverage values significantly below the confidence level indicate that the intervals are too narrow (overconfident forecasts), while values significantly above indicate that the intervals are too wide (underconfident forecasts). Coverage is a key metric for assessing the reliability of uncertainty quantification in forecasts.

**Example:**

```sql
SELECT 
    product_id,
    TS_COVERAGE(LIST(actual), LIST(lower), LIST(upper)) * 100 AS coverage_pct
FROM results
GROUP BY product_id;
-- Coverage should be close to confidence_level × 100
```

---

## Error Handling

All functions validate inputs:

```sql
-- ERROR: Arrays must have same length
SELECT TS_MAE([1, 2, 3], [1, 2]);

-- ERROR: Arrays must not be empty
SELECT TS_MAE([], []);

-- ERROR: MAPE undefined for zeros
SELECT TS_MAPE([0, 1, 2], [0, 1, 2]);
-- Returns NULL (gracefully handled)

-- ERROR: MASE requires 3 arguments
SELECT TS_MASE([1, 2], [1, 2]);
-- Use: TS_MASE([1, 2], [1, 2], baseline)
```
