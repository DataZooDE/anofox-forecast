# Time Series Evaluation Metrics

## Introduction

The anofox-forecast extension provides a comprehensive set of evaluation metric functions for assessing forecast accuracy and model performance. These metrics enable quantitative comparison of forecasting methods, evaluation of prediction intervals, and assessment of forecast bias across single or multiple time series.

**Key Capabilities**:

- 12 evaluation metrics covering error, percentage error, scaled error, and probabilistic forecast evaluation
- Support for GROUP BY operations using DuckDB's LIST() aggregation
- Scale-independent metrics for comparing forecasts across different time series
- Probabilistic forecast evaluation with quantile loss and prediction interval coverage
- Input validation and error handling for robust metric computation

---

## Table of Contents

1. [Example: Forecasting and Evaluation with Multiple Methods](#example-forecasting-and-evaluation-with-multiple-methods)
2. [Using Metrics with GROUP BY](#using-metrics-with-group-by)
3. [Function Signatures](#function-signatures)
   - [anofox_fcst_ts_mae - Mean Absolute Error](#ts_mae---mean-absolute-error)
   - [anofox_fcst_ts_mse - Mean Squared Error](#ts_mse---mean-squared-error)
   - [anofox_fcst_ts_rmse - Root Mean Squared Error](#ts_rmse---root-mean-squared-error)
   - [anofox_fcst_ts_mape - Mean Absolute Percentage Error](#ts_mape---mean-absolute-percentage-error)
   - [anofox_fcst_ts_smape - Symmetric Mean Absolute Percentage Error](#ts_smape---symmetric-mean-absolute-percentage-error)
   - [anofox_fcst_ts_mase - Mean Absolute Scaled Error](#ts_mase---mean-absolute-scaled-error)
   - [TS_R2 - Coefficient of Determination](#ts_r2---coefficient-of-determination)
   - [anofox_fcst_ts_bias - Forecast Bias](#ts_bias---forecast-bias)
   - [anofox_fcst_ts_rmae - Relative Mean Absolute Error](#ts_rmae---relative-mean-absolute-error)
   - [anofox_fcst_ts_quantile_loss - Quantile Loss](#ts_quantile_loss---quantile-loss-pinball-loss)
   - [anofox_fcst_ts_mqloss - Multi-Quantile Loss](#ts_mqloss---multi-quantile-loss)
   - [anofox_fcst_ts_coverage - Prediction Interval Coverage](#ts_coverage---prediction-interval-coverage)
4. [Error Handling](#error-handling)

---

## Example: Forecasting and Evaluation with Multiple Methods

The following complete example demonstrates creating train and test datasets, generating forecasts using three different forecasting methods for multiple time series with GROUP BY, then evaluating the forecasts using `anofox_fcst_ts_mae` and `anofox_fcst_ts_bias` metrics. This example is copy-paste ready and can be executed immediately.

```sql
-- Step 1: Create training data with multiple products and weekly seasonality
CREATE TABLE sales_train AS
SELECT 
    'Product_' || LPAD((i % 3 + 1)::VARCHAR, 2, '0') AS product_id,
    DATE '2024-01-01' + INTERVAL (d) DAY AS date,
    GREATEST(0, 
        100.0 + (i % 3 + 1) * 20.0  -- Base level varies by product
        + 0.5 * d  -- Linear trend
        + 15.0 * SIN(2 * PI() * d / 7)  -- Weekly seasonality
        + (RANDOM() * 10.0 - 5.0)  -- Random noise
    )::DOUBLE AS revenue
FROM generate_series(0, 89) t(d)  -- 90 days of training data
CROSS JOIN generate_series(1, 3) t(i);  -- 3 products

-- Step 2: Create test data (holdout period)
CREATE TABLE sales_test AS
SELECT 
    'Product_' || LPAD((i % 3 + 1)::VARCHAR, 2, '0') AS product_id,
    DATE '2024-04-01' + INTERVAL (d) DAY AS date,
    GREATEST(0, 
        100.0 + (i % 3 + 1) * 20.0  -- Base level varies by product
        + 0.5 * (d + 90)  -- Linear trend (continuing from train)
        + 15.0 * SIN(2 * PI() * (d + 90) / 7)  -- Weekly seasonality
        + (RANDOM() * 10.0 - 5.0)  -- Random noise
    )::DOUBLE AS revenue
FROM generate_series(0, 27) t(d)  -- 28 days of test data
CROSS JOIN generate_series(1, 3) t(i);  -- 3 products

-- Step 3: Generate forecasts using three different methods for each product
CREATE TEMP TABLE forecasts AS
SELECT * FROM anofox_fcst_ts_forecast_by(
    'sales_train',
    product_id,
    date,
    revenue,
    'AutoETS',
    28,
    MAP{'seasonal_period': 7}
)
UNION ALL
SELECT * FROM anofox_fcst_ts_forecast_by(
    'sales_train',
    product_id,
    date,
    revenue,
    'SeasonalNaive',
    28,
    MAP{'seasonal_period': 7}
)
UNION ALL
SELECT * FROM anofox_fcst_ts_forecast_by(
    'sales_train',
    product_id,
    date,
    revenue,
    'AutoARIMA',
    28,
    MAP{'seasonal_period': 7}
);

-- Step 4: Join forecasts with actual test data
CREATE TEMP TABLE evaluation_data AS
SELECT 
    f.product_id,
    f.model_name,
    f.date,
    f.point_forecast,
    t.revenue AS actual_value
FROM forecasts f
JOIN sales_test t ON f.product_id = t.product_id AND f.date = t.date;

-- Step 5: Evaluate forecasts using anofox_fcst_ts_mae and anofox_fcst_ts_bias per product and model
SELECT 
    product_id,
    model_name,
    anofox_fcst_ts_mae(LIST(actual_value), LIST(point_forecast)) AS mae,
    anofox_fcst_ts_bias(LIST(actual_value), LIST(point_forecast)) AS bias
FROM evaluation_data
GROUP BY product_id, model_name
ORDER BY product_id, mae;
```

This example demonstrates:

- Creating training data with multiple time series (3 products) with weekly seasonality and trend
- Creating test data (holdout period) for evaluation
- Generating forecasts for multiple time series using `anofox_fcst_ts_forecast_by` with three different methods (AutoETS, SeasonalNaive, AutoARIMA)
- Combining forecasts from multiple methods using `UNION ALL`
- Joining forecast results with actual test data
- Evaluating forecast accuracy using `anofox_fcst_ts_mae` and `anofox_fcst_ts_bias` with `GROUP BY` and `LIST()` aggregation
- Results are grouped by both `product_id` and `model_name` to compare method performance across products

[↑ Go to top](#time-series-evaluation-metrics)

---

## Using Metrics with GROUP BY

All metrics work seamlessly with `GROUP BY` operations using DuckDB's `LIST()` aggregate function.

### Pattern

**Metrics expect arrays (LIST), not individual values:**

```sql
-- Create sample forecast results
CREATE TABLE results AS
SELECT 
    1 AS forecast_step,
    100.0 AS actual,
    102.5 AS forecast,
    95.0 AS lower,
    110.0 AS upper
UNION ALL
SELECT 2, 105.0, 104.0, 96.0, 112.0
UNION ALL
SELECT 3, 103.0, 105.5, 97.0, 114.0
UNION ALL
SELECT 4, 108.0, 107.0, 98.0, 116.0
UNION ALL
SELECT 5, 106.0, 108.5, 99.0, 118.0;

-- ❌ WRONG - This won't work (metrics need arrays)
-- SELECT product_id, anofox_fcst_ts_mae(actual, predicted)
-- FROM results
-- GROUP BY product_id;

-- ✅ CORRECT - Use LIST() to create arrays
SELECT 
    anofox_fcst_ts_mae(LIST(actual ORDER BY forecast_step), LIST(forecast ORDER BY forecast_step)) AS mae
FROM results;
```

[↑ Go to top](#time-series-evaluation-metrics)

---

## Function Signatures

### anofox_fcst_ts_mae - Mean Absolute Error

**Signature:**

```sql
anofox_fcst_ts_mae(
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
SELECT anofox_fcst_ts_mae([100, 102, 105], [101, 101, 104]) AS mae;
-- Result: 0.67
```

[↑ Go to top](#time-series-evaluation-metrics)

---

### anofox_fcst_ts_mse - Mean Squared Error

**Signature:**

```sql
anofox_fcst_ts_mse(
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
SELECT anofox_fcst_ts_mse([100, 102, 105], [101, 101, 104]) AS mse;
-- Result: 0.67
```

[↑ Go to top](#time-series-evaluation-metrics)

---

### anofox_fcst_ts_rmse - Root Mean Squared Error

**Signature:**

```sql
anofox_fcst_ts_rmse(
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
SELECT anofox_fcst_ts_rmse([100, 102, 105], [101, 101, 104]) AS rmse;
-- Result: 0.82
```

[↑ Go to top](#time-series-evaluation-metrics)

---

### anofox_fcst_ts_mape - Mean Absolute Percentage Error

**Signature:**

```sql
anofox_fcst_ts_mape(
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
SELECT anofox_fcst_ts_mape([100, 102, 105], [101, 101, 104]) AS mape_percent;
-- Result: 0.65 (0.65% error)
```

[↑ Go to top](#time-series-evaluation-metrics)

---

### anofox_fcst_ts_smape - Symmetric Mean Absolute Percentage Error

**Signature:**

```sql
anofox_fcst_ts_smape(
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
SELECT anofox_fcst_ts_smape([100, 102, 105], [101, 101, 104]) AS smape_percent;
-- Result: 0.65 (0.65% error)
```

[↑ Go to top](#time-series-evaluation-metrics)

---

### anofox_fcst_ts_mase - Mean Absolute Scaled Error

**Signature:**

```sql
anofox_fcst_ts_mase(
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
SELECT anofox_fcst_ts_mase(
    [100, 102, 105, 103, 107],  -- actual
    [101, 101, 104, 104, 106],  -- theta predictions
    [100, 100, 100, 100, 100]   -- naive baseline
) AS mase;
-- Result: 0.24 → Theta is 76% better than Naive ✅
```

[↑ Go to top](#time-series-evaluation-metrics)

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
SELECT anofox_fcst_ts_r2([100, 102, 105, 103, 107], [101, 101, 104, 104, 106]) AS r_squared;
-- Result: 0.88 → Model explains 88% of variance
```

[↑ Go to top](#time-series-evaluation-metrics)

---

### anofox_fcst_ts_bias - Forecast Bias

**Signature:**

```sql
anofox_fcst_ts_bias(
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
SELECT anofox_fcst_ts_bias([100, 102, 105], [103, 105, 108]) AS bias;
-- Result: +3.0 → Systematically over-forecasting by 3 units

-- Under-forecasting
SELECT anofox_fcst_ts_bias([100, 102, 105], [98, 100, 103]) AS bias;
-- Result: -2.0 → Systematically under-forecasting by 2 units

-- Unbiased
SELECT anofox_fcst_ts_bias([100, 102, 105], [101, 101, 106]) AS bias;
-- Result: 0.0 → Errors cancel out (no systematic bias)
```

[↑ Go to top](#time-series-evaluation-metrics)

---

### anofox_fcst_ts_rmae - Relative Mean Absolute Error

**Signature:**

```sql
anofox_fcst_ts_rmae(
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
-- Create sample forecast comparison data
CREATE TABLE forecast_comparison AS
SELECT 
    [100.0, 102.0, 98.0, 105.0]::DOUBLE[] AS actual,
    [101.0, 103.0, 99.0, 106.0]::DOUBLE[] AS forecast_autoets,
    [100.0, 100.0, 100.0, 100.0]::DOUBLE[] AS forecast_naive;

-- Compare AutoETS vs Naive forecast
SELECT 
    anofox_fcst_ts_mae(actual, forecast_autoets) AS mae_autoets,
    anofox_fcst_ts_mae(actual, forecast_naive) AS mae_naive,
    anofox_fcst_ts_rmae(actual, forecast_autoets, forecast_naive) AS relative_performance,
    CASE 
        WHEN anofox_fcst_ts_rmae(actual, forecast_autoets, forecast_naive) < 1.0
        THEN 'AutoETS is better'
        ELSE 'Naive is better'
    END AS winner
FROM forecast_comparison;

-- Result: 
-- mae_autoets: 2.0, mae_naive: 4.5, relative_performance: 0.44, winner: 'AutoETS is better'
```

[↑ Go to top](#time-series-evaluation-metrics)

---

### anofox_fcst_ts_quantile_loss - Quantile Loss (Pinball Loss)

**Signature:**

```sql
anofox_fcst_ts_quantile_loss(
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
<!-- include: test/sql/docs_examples/50_evaluation_metrics_evaluate_36.sql -->

[↑ Go to top](#time-series-evaluation-metrics)

---

### anofox_fcst_ts_mqloss - Multi-Quantile Loss

**Signature:**

```sql
anofox_fcst_ts_mqloss(
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
    anofox_fcst_ts_mqloss(actual, predicted_quantiles, quantiles) AS mqloss,
    'Lower is better - measures full distribution accuracy' AS interpretation
FROM distributions;

-- Result: mqloss = 0.9 (good distribution fit)
```

**Use Case - CRPS Approximation**:
<!-- include: test/sql/docs_examples/50_evaluation_metrics_evaluate_39.sql -->

[↑ Go to top](#time-series-evaluation-metrics)

---

### anofox_fcst_ts_coverage - Prediction Interval Coverage

**Signature:**

```sql
anofox_fcst_ts_coverage(
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
    anofox_fcst_ts_coverage(LIST(actual), LIST(lower), LIST(upper)) * 100 AS coverage_pct
FROM results
GROUP BY product_id;
-- Coverage should be close to confidence_level × 100
```

[↑ Go to top](#time-series-evaluation-metrics)

---

## Error Handling

All functions validate inputs:

<!-- include: test/sql/docs_examples/50_evaluation_metrics_example_40.sql -->

[↑ Go to top](#time-series-evaluation-metrics)
