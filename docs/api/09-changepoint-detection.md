# Changepoint Detection

> Detect structural breaks in time series

## Overview

Changepoint detection identifies points in time where the statistical properties of a time series change significantly.

---

## Scalar Functions

### ts_detect_changepoints

Detects structural breaks in time series using PELT algorithm.

**Signature:**
```sql
ts_detect_changepoints(values DOUBLE[]) → STRUCT
ts_detect_changepoints(values DOUBLE[], min_size INTEGER, penalty DOUBLE) → STRUCT
```

**Parameters:**
- `values`: Input time series
- `min_size`: Minimum segment size (default: 2)
- `penalty`: Penalty for adding changepoints (default: auto)

**Returns:**
```sql
STRUCT(
    changepoints    UBIGINT[],  -- Indices of changepoints
    n_changepoints  UBIGINT,    -- Number of changepoints detected
    cost            DOUBLE      -- Total cost of segmentation
)
```

**Example:**
```sql
-- Detect level shift
SELECT ts_detect_changepoints([1,1,1,1,1,10,10,10,10,10]::DOUBLE[]);
-- Returns: {changepoints: [5], n_changepoints: 1, cost: ...}

-- With custom parameters
SELECT ts_detect_changepoints([1,1,1,1,1,10,10,10,10,10]::DOUBLE[], 3, 1.0);
```

---

### ts_detect_changepoints_bocpd

Bayesian Online Changepoint Detection with Normal-Gamma conjugate prior.

**Signature:**
```sql
ts_detect_changepoints_bocpd(
    values DOUBLE[],
    hazard_lambda DOUBLE,
    include_probabilities BOOLEAN
) → STRUCT
```

**Parameters:**
- `values`: Input time series
- `hazard_lambda`: Expected run length between changepoints
- `include_probabilities`: Include per-point probabilities

**Returns:**
```sql
STRUCT(
    is_changepoint           BOOLEAN[],   -- Per-point flags
    changepoint_probability  DOUBLE[],    -- Per-point probabilities
    changepoint_indices      UBIGINT[]    -- Indices of changepoints
)
```

**Example:**
```sql
SELECT ts_detect_changepoints_bocpd(
    [1,1,1,1,1,10,10,10,10,10]::DOUBLE[],
    100.0,   -- expect changepoint every ~100 observations
    true     -- include probabilities
);
```

---

## Aggregate Functions

### ts_detect_changepoints_agg

Aggregate function for detecting changepoints in grouped time series.

**Signature:**
```sql
ts_detect_changepoints_agg(
    timestamp_col TIMESTAMP,
    value_col DOUBLE,
    params MAP(VARCHAR, VARCHAR)
) → LIST<STRUCT>
```

**Parameters in MAP:**
- `hazard_lambda`: Hazard rate parameter (default: 250.0)
- `include_probabilities`: Include per-point probabilities (default: false)

**Returns:**
```sql
LIST<STRUCT(
    timestamp              TIMESTAMP,
    value                  DOUBLE,
    is_changepoint         BOOLEAN,
    changepoint_probability DOUBLE
)>
```

**Example:**
```sql
SELECT
    product_id,
    ts_detect_changepoints_agg(date, value, MAP{}) AS changepoints
FROM sales
GROUP BY product_id;
```

---

## Table Macros

### ts_detect_changepoints (Table Macro)

Detect changepoints across multiple series.

**Signature:**
```sql
ts_detect_changepoints(source, group_col, date_col, value_col, params)
```

---

### ts_detect_changepoints_by

Detect changepoints for each group in a table.

**Signature:**
```sql
ts_detect_changepoints_by(source, group_col, date_col, value_col, params)
```

**Example:**
```sql
SELECT * FROM ts_detect_changepoints_by(
    'sales',
    product_id,
    date,
    value,
    MAP{'hazard_lambda': '100'}
);
```

---

## Algorithm Details

### PELT (Pruned Exact Linear Time)
- Default algorithm for `ts_detect_changepoints`
- Optimal segmentation with linear time complexity
- Uses penalty to control number of changepoints

### BOCPD (Bayesian Online Changepoint Detection)
- Online algorithm for streaming data
- Provides probability estimates for each point
- Configurable hazard rate for expected changepoint frequency

---

## Use Cases

```sql
-- Detect regime changes in price data
SELECT
    symbol,
    (ts_detect_changepoints(LIST(price ORDER BY date))).changepoints AS breaks
FROM stock_prices
GROUP BY symbol;

-- Filter to series with changepoints
WITH cp AS (
    SELECT
        product_id,
        ts_detect_changepoints(LIST(sales ORDER BY date)) AS result
    FROM sales_data
    GROUP BY product_id
)
SELECT product_id, result.changepoints
FROM cp
WHERE result.n_changepoints > 0;

-- Get changepoint timestamps
SELECT
    product_id,
    dates[unnest(result.changepoints)] AS changepoint_dates
FROM (
    SELECT
        product_id,
        LIST(date ORDER BY date) AS dates,
        ts_detect_changepoints(LIST(value ORDER BY date)) AS result
    FROM sales
    GROUP BY product_id
);
```

---

*See also: [Statistics](02-statistics.md) | [Feature Extraction](08-feature-extraction.md)*
