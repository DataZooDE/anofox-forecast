# DuckDB Time Series Extension - Final Design (v3.0)

## Key Design: Table-In-Out Functions + GROUP BY

**Best approach:** Use DuckDB's `table_in_out_function` for the perfect combination of GROUP BY parallelization and table output!

This design leverages DuckDB's [table_in_out_function](https://github.com/duckdb/duckdb/blob/56cfbb0c2b53fa84b4eb09fb21034357a68195fd/src/include/duckdb/function/table_function.hpp#L283) which provides:

✅ **GROUP BY parallelization** (automatic)  
✅ **Table output** (no unnesting needed)  
✅ **Standard SQL** (familiar patterns)  
✅ **Optimal performance** (DuckDB optimized)  
✅ **Clean implementation** (less code)  

---

## Core Insight: Table-In-Out Functions

Instead of aggregate functions that return structs, use **table-in-out functions** that:
1. Accept grouped input (via GROUP BY)
2. Process each group in parallel (DuckDB handles this)
3. Return tabular output directly (no unnesting)

### Why This is the Best Approach

✅ **Best of both worlds**: GROUP BY parallelization + table output  
✅ **No unnesting**: Direct tabular results  
✅ **Familiar SQL**: Standard table function syntax  
✅ **DuckDB optimized**: Uses native parallelization  
✅ **Cleaner code**: Less complex than aggregates  

---

## Revised API Design

### 1. FORECAST() - Table-In-Out Function (Primary Interface)

Forecasting as a **table-in-out function** that works with GROUP BY.

#### Signature
```sql
FORECAST(
    timestamp_col TIMESTAMP,
    value_col DOUBLE,
    model VARCHAR,
    horizon INTEGER,
    model_params STRUCT DEFAULT {}
) → TABLE (
    forecast_step INTEGER,     -- 1, 2, ..., horizon
    point_forecast DOUBLE,     -- Point forecast
    lower_95 DOUBLE,          -- Lower prediction interval
    upper_95 DOUBLE,          -- Upper prediction interval
    model_name VARCHAR,       -- Actual model used
    fit_time_ms DOUBLE,       -- Fitting time
    aic DOUBLE,               -- AIC (if available)
    bic DOUBLE,               -- BIC (if available)
    aicc DOUBLE               -- AICc (if available)
)
```

#### Example: Single Series
```sql
-- Forecast single series - returns table directly
SELECT * FROM FORECAST(
    (SELECT date, sales FROM sales),
    'date',
    'sales',
    'AutoETS',
    12,
    {'seasonal_period': 12}
);

-- Result: 12 rows (one per forecast step)
-- forecast_step | point_forecast | lower_95 | upper_95 | model_name | ...
-- 1            | 1250.5        | 1180.2   | 1320.8   | AutoETS    | ...
-- 2            | 1275.3        | 1195.1   | 1355.5   | AutoETS    | ...
-- ...
```

#### Example: **Batch with GROUP BY** 🎯
```sql
-- DuckDB automatically parallelizes GROUP BY!
SELECT 
    product_id,
    forecast_step,
    point_forecast,
    lower_95,
    upper_95,
    model_name
FROM FORECAST(
    sales,
    'date',
    'sales',
    'AutoETS',
    12,
    {'seasonal_period': 12}
)
GROUP BY product_id;

-- DuckDB's engine automatically:
-- 1. Partitions products across threads
-- 2. Processes each group in parallel
-- 3. Returns tabular results directly
-- No unnesting needed!
```

### 2. ENSEMBLE() - Table-In-Out Function

Ensemble forecasting as a table-in-out function.

#### Signature
```sql
ENSEMBLE(
    timestamp_col TIMESTAMP,
    value_col DOUBLE,
    models VARCHAR[],
    horizon INTEGER,
    method VARCHAR DEFAULT 'Mean',
    ensemble_config STRUCT DEFAULT {}
) → TABLE (
    forecast_step INTEGER,
    point_forecast DOUBLE,
    lower_95 DOUBLE,
    upper_95 DOUBLE,
    weights STRUCT,              -- Weight per model
    individual_forecasts STRUCT  -- Forecast from each model
)
```

#### Example: Ensemble with GROUP BY
```sql
-- Mean ensemble for each product (parallel!)
SELECT 
    product_id,
    forecast_step,
    point_forecast,
    lower_95,
    upper_95
FROM ENSEMBLE(
    sales,
    'date',
    'sales',
    ['Naive', 'SES', 'Theta', 'AutoETS'],
    12,
    'Mean'
)
GROUP BY product_id;

-- DuckDB parallelizes across products automatically!
-- Returns tabular results directly!
```

### 3. BACKTEST() - Table-In-Out Function

Cross-validation as a table-in-out function.

#### Signature
```sql
BACKTEST(
    timestamp_col TIMESTAMP,
    value_col DOUBLE,
    model VARCHAR,
    backtest_config STRUCT,
    model_params STRUCT DEFAULT {}
) → TABLE (
    fold INTEGER,              -- Fold number
    train_start INTEGER,       -- Training start index
    train_end INTEGER,         -- Training end index
    test_start INTEGER,        -- Test start index
    test_end INTEGER,          -- Test end index
    mae DOUBLE,               -- MAE for this fold
    rmse DOUBLE,              -- RMSE for this fold
    mape DOUBLE,              -- MAPE for this fold
    avg_mae DOUBLE,           -- Average MAE across folds
    avg_rmse DOUBLE,          -- Average RMSE across folds
    avg_mape DOUBLE           -- Average MAPE across folds
)
```

#### Example: Backtest with GROUP BY
```sql
-- Backtest each product in parallel
SELECT 
    product_id,
    fold,
    mae,
    rmse,
    avg_mae
FROM BACKTEST(
    sales,
    'date',
    'sales',
    'Theta',
    {'min_train': 24, 'horizon': 12, 'step': 6, 'max_folds': 5},
    {'seasonal_period': 12}
)
GROUP BY product_id;

-- DuckDB parallelizes across products automatically!
```

---

## How Table-In-Out Functions Work

### DuckDB's Table-In-Out Function Interface

Based on the [DuckDB table function interface](https://github.com/duckdb/duckdb/blob/56cfbb0c2b53fa84b4eb09fb21034357a68195fd/src/include/duckdb/function/table_function.hpp#L283), we implement:

```cpp
// Table-in-out function that processes grouped data
class ForecastTableInOutFunction : public TableFunction {
public:
    ForecastTableInOutFunction() {
        name = "forecast";
        bind = ForecastBind;
        init_global = ForecastInitGlobal;
        init_local = ForecastInitLocal;
        function = ForecastFunction;
        cardinality = ForecastCardinality;
        dependency = ForecastDependency;
        pushdown_complex_filter = ForecastPushdownComplexFilter;
        table_scan_progress = ForecastTableScanProgress;
        get_batch_index = ForecastGetBatchIndex;
        serialize = ForecastSerialize;
        deserialize = ForecastDeserialize;
        get_bind_info = ForecastGetBindInfo;
    }
};
```

### Internal Execution with GROUP BY

```sql
SELECT 
    product_id,
    forecast_step,
    point_forecast
FROM FORECAST(sales, 'date', 'sales', 'Theta', 12)
GROUP BY product_id;
```

**DuckDB's Query Plan:**

```
PROJECTION
    │
    ▼
HASH_GROUP_BY (PARALLEL)
    │
    ├─► Partition 1 (Thread 1) ──┐
    ├─► Partition 2 (Thread 2) ──┤
    ├─► Partition 3 (Thread 3) ──┼─► Hash on product_id
    ├─► ...                      │
    └─► Partition N (Thread N) ──┘
         │
         └─► For each group in partition:
             1. Accumulate (timestamp, value) pairs
             2. When group complete, call FORECAST function
             3. Fit model and generate forecast
             4. Return table rows (not struct)
    
Results from all threads combined automatically
```

**We get parallelization + table output for free!** 🎉

---

## Implementation Details

### Table-In-Out Function Implementation

```cpp
// Bind: Analyze input and determine output schema
static unique_ptr<FunctionData> ForecastBind(
    ClientContext &context,
    TableFunctionBindInput &input,
    vector<LogicalType> &return_types,
    vector<string> &names
) {
    // Extract parameters
    auto &inputs = input.inputs;
    auto table_name = inputs[0].GetValue<string>();
    auto timestamp_col = inputs[1].GetValue<string>();
    auto value_col = inputs[2].GetValue<string>();
    auto model = inputs[3].GetValue<string>();
    auto horizon = inputs[4].GetValue<int32_t>();
    auto params = inputs[5].GetValue<Value>();
    
    // Set output schema
    return_types = {
        LogicalType::INTEGER,    // forecast_step
        LogicalType::DOUBLE,     // point_forecast
        LogicalType::DOUBLE,     // lower_95
        LogicalType::DOUBLE,     // upper_95
        LogicalType::VARCHAR,    // model_name
        LogicalType::DOUBLE,     // fit_time_ms
        LogicalType::DOUBLE,     // aic
        LogicalType::DOUBLE,     // bic
        LogicalType::DOUBLE      // aicc
    };
    
    names = {
        "forecast_step",
        "point_forecast", 
        "lower_95",
        "upper_95",
        "model_name",
        "fit_time_ms",
        "aic",
        "bic",
        "aicc"
    };
    
    // Store configuration
    auto bind_data = make_uniq<ForecastBindData>();
    bind_data->table_name = table_name;
    bind_data->timestamp_col = timestamp_col;
    bind_data->value_col = value_col;
    bind_data->model_name = model;
    bind_data->horizon = horizon;
    bind_data->model_params = params;
    
    return std::move(bind_data);
}

// Init Global: Called once per query
static unique_ptr<GlobalTableFunctionState> ForecastInitGlobal(
    ClientContext &context,
    TableFunctionInitInput &input
) {
    auto &bind_data = input.bind_data->Cast<ForecastBindData>();
    
    auto global_state = make_uniq<ForecastGlobalState>();
    global_state->bind_data = &bind_data;
    
    // Get table info
    auto &catalog = Catalog::GetSystemCatalog(*context.db);
    auto table_info = catalog.GetEntry<TableCatalogEntry>(context, DEFAULT_SCHEMA, bind_data->table_name);
    global_state->table_info = table_info;
    
    return std::move(global_state);
}

// Init Local: Called once per thread
static unique_ptr<LocalTableFunctionState> ForecastInitLocal(
    ExecutionContext &context,
    TableFunctionInitInput &input,
    GlobalTableFunctionState *global_state
) {
    auto local_state = make_uniq<ForecastLocalState>();
    local_state->global_state = (ForecastGlobalState*)global_state;
    return std::move(local_state);
}

// Function: Process each group
static void ForecastFunction(
    ClientContext &context,
    TableFunctionInput &data_p,
    DataChunk &output
) {
    auto &state = data_p.local_state->Cast<ForecastLocalState>();
    auto &global_state = state.global_state;
    
    // Get current group data
    if (!state.current_group_data) {
        // This is where DuckDB provides the grouped data
        // We need to accumulate the time series for this group
        state.current_group_data = AccumulateGroupData(context, global_state);
    }
    
    if (!state.current_group_data) {
        // No more groups
        return;
    }
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    try {
        // Build time series
        auto ts = BuildTimeSeries(state.current_group_data->timestamps, 
                                 state.current_group_data->values);
        
        // Create and fit model
        auto model = ModelFactory::Create(global_state->bind_data->model_name, 
                                        global_state->bind_data->model_params);
        model->fit(ts);
        
        // Generate forecast
        auto forecast = model->predict(global_state->bind_data->horizon);
        
        // Fill output chunk with forecast rows
        idx_t output_count = 0;
        for (int h = 0; h < global_state->bind_data->horizon; h++) {
            output.data[0].SetValue(output_count, Value::INTEGER(h + 1)); // forecast_step
            output.data[1].SetValue(output_count, Value::DOUBLE(forecast.primary()[h])); // point_forecast
            
            if (forecast.lower.has_value()) {
                output.data[2].SetValue(output_count, Value::DOUBLE((*forecast.lower)[0][h])); // lower_95
                output.data[3].SetValue(output_count, Value::DOUBLE((*forecast.upper)[0][h])); // upper_95
            } else {
                output.data[2].SetValue(output_count, Value::DOUBLE(forecast.primary()[h])); // fallback
                output.data[3].SetValue(output_count, Value::DOUBLE(forecast.primary()[h])); // fallback
            }
            
            output.data[4].SetValue(output_count, Value(model->getName())); // model_name
            
            auto end_time = std::chrono::high_resolution_clock::now();
            auto fit_time = std::chrono::duration<double, std::milli>(end_time - start_time).count();
            output.data[5].SetValue(output_count, Value::DOUBLE(fit_time)); // fit_time_ms
            
            // Extract AIC/BIC/AICc
            std::optional<double> aic, bic, aicc;
            ExtractInformationCriteria(model.get(), ts.size(), aic, bic, aicc);
            
            output.data[6].SetValue(output_count, aic ? Value::DOUBLE(*aic) : Value()); // aic
            output.data[7].SetValue(output_count, bic ? Value::DOUBLE(*bic) : Value()); // bic
            output.data[8].SetValue(output_count, aicc ? Value::DOUBLE(*aicc) : Value()); // aicc
            
            output_count++;
        }
        
        output.SetCardinality(output_count);
        
        // Mark this group as processed
        state.current_group_data = nullptr;
        
    } catch (const std::exception& e) {
        // Error handling - could return empty chunk or error rows
        output.SetCardinality(0);
        state.current_group_data = nullptr;
    }
}

// Cardinality: Tell DuckDB how many rows this group will produce
static unique_ptr<NodeStatistics> ForecastCardinality(
    ClientContext &context,
    const FunctionData *bind_data
) {
    auto &forecast_bind = bind_data->Cast<ForecastBindData>();
    auto cardinality = forecast_bind.horizon; // Each group produces 'horizon' rows
    
    return make_uniq<NodeStatistics>(cardinality);
}
```

### State Management

```cpp
// Global state (per query)
struct ForecastGlobalState : public GlobalTableFunctionState {
    const ForecastBindData* bind_data;
    TableCatalogEntry* table_info;
};

// Local state (per thread)
struct ForecastLocalState : public LocalTableFunctionState {
    ForecastGlobalState* global_state;
    std::unique_ptr<GroupData> current_group_data;
};

// Group data (accumulated time series)
struct GroupData {
    std::vector<anofoxtime::core::TimeSeries::TimePoint> timestamps;
    std::vector<double> values;
    std::string group_key; // For debugging
};
```

---

## Usage Examples

### Example 1: Simple Batch Forecast

```sql
-- Forecast all products (DuckDB parallelizes automatically!)
SELECT 
    product_id,
    forecast_step,
    point_forecast,
    lower_95,
    upper_95,
    model_name
FROM FORECAST(
    sales,
    'date',
    'sales',
    'Theta',
    12,
    {'seasonal_period': 12}
)
GROUP BY product_id
ORDER BY product_id, forecast_step;

-- Result: 12 rows per product
-- product_id | forecast_step | point_forecast | lower_95 | upper_95 | model_name
-- 1         | 1            | 1250.5        | 1180.2   | 1320.8   | Theta
-- 1         | 2            | 1275.3        | 1195.1   | 1355.5   | Theta
-- ...
-- 2         | 1            | 2100.2        | 1980.1   | 2220.3   | Theta
-- 2         | 2            | 2125.8        | 1995.7   | 2255.9   | Theta
-- ...
```

### Example 2: Ensemble with GROUP BY

```sql
-- Ensemble forecast for each store
SELECT 
    store_id,
    forecast_step,
    point_forecast,
    lower_95,
    upper_95
FROM ENSEMBLE(
    store_sales,
    'date',
    'daily_revenue',
    ['Naive', 'SES', 'Theta', 'AutoETS'],
    14,
    'WeightedAccuracy',
    {
        'accuracy_metric': 'MAE',
        'validation_split': 0.2,
        'temperature': 0.5
    }
)
GROUP BY store_id
ORDER BY store_id, forecast_step;

-- DuckDB parallelizes across stores automatically!
-- Returns tabular results directly!
```

### Example 3: Hierarchical Forecasting

```sql
-- SKU level (bottom)
CREATE TABLE sku_forecasts AS
SELECT 
    sku_id,
    product_id,
    region,
    forecast_step,
    point_forecast,
    lower_95,
    upper_95
FROM FORECAST(
    sku_sales,
    'date',
    'units',
    'AutoETS',
    12,
    {'seasonal_period': 12}
)
GROUP BY sku_id, product_id, region;

-- Product level (middle) - aggregate SKUs
CREATE TABLE product_forecasts AS
SELECT 
    product_id,
    region,
    forecast_step,
    SUM(point_forecast) as point_forecast,
    SUM(lower_95) as lower_95,
    SUM(upper_95) as upper_95
FROM sku_forecasts
GROUP BY product_id, region, forecast_step;

-- Region level (top) - aggregate products
CREATE TABLE region_forecasts AS
SELECT 
    region,
    forecast_step,
    SUM(point_forecast) as point_forecast,
    SUM(lower_95) as lower_95,
    SUM(upper_95) as upper_95
FROM product_forecasts
GROUP BY region, forecast_step;

-- All levels use standard GROUP BY - clean and simple!
```

### Example 4: Model Comparison

```sql
-- Test 4 models on each product
WITH model_tests AS (
    SELECT 
        p.product_id,
        m.model_name,
        b.fold,
        b.mae,
        b.rmse
    FROM products p
    CROSS JOIN (VALUES ('Naive'), ('SES'), ('Theta'), ('AutoETS')) m(model_name)
    CROSS JOIN LATERAL (
        SELECT * FROM BACKTEST(
            sales,
            'date',
            'sales',
            m.model_name,
            {'min_train': 24, 'horizon': 12, 'step': 12, 'max_folds': 3}
        )
        WHERE sales.product_id = p.product_id
    ) b
),
best_models AS (
    SELECT 
        product_id,
        ARG_MIN(model_name, avg_mae) as best_model
    FROM model_tests
    GROUP BY product_id
)
-- Forecast with best model
SELECT 
    p.product_id,
    b.best_model,
    f.forecast_step,
    f.point_forecast,
    f.lower_95,
    f.upper_95
FROM best_models b
CROSS JOIN LATERAL (
    SELECT * FROM FORECAST(
        sales,
        'date',
        'sales',
        b.best_model,
        12
    )
    WHERE sales.product_id = b.product_id
) f
ORDER BY p.product_id, f.forecast_step;
```

---

## Advantages of Table-In-Out Functions

### 1. Best of Both Worlds

```sql
-- ✅ GROUP BY parallelization (automatic)
-- ✅ Table output (no unnesting)
-- ✅ Standard SQL syntax
-- ✅ DuckDB optimized

SELECT 
    product_id,
    forecast_step,
    point_forecast
FROM FORECAST(sales, 'date', 'sales', 'Theta', 12)
GROUP BY product_id;
```

### 2. No Unnesting Required

```sql
-- ❌ Aggregate approach (requires unnesting):
WITH forecasts AS (
    SELECT 
        product_id,
        TS_FORECAST(date, sales, 'Theta', 12) as f
    FROM sales
    GROUP BY product_id
)
SELECT 
    product_id,
    unnest(generate_series(1, 12)) as forecast_step,
    unnest(f.point) as point_forecast
FROM forecasts;

-- ✅ Table-in-out approach (direct table output):
SELECT 
    product_id,
    forecast_step,
    point_forecast
FROM FORECAST(sales, 'date', 'sales', 'Theta', 12)
GROUP BY product_id;
```

### 3. Natural SQL Patterns

```sql
-- Works naturally with all SQL features:

-- WITH FILTERING
SELECT * FROM FORECAST(sales, 'date', 'sales', 'Theta', 12)
WHERE sales.date >= '2020-01-01'
GROUP BY product_id;

-- WITH ORDERING
SELECT * FROM FORECAST(sales, 'date', 'sales', 'Theta', 12)
GROUP BY product_id
ORDER BY product_id, forecast_step;

-- WITH JOINS
SELECT 
    p.product_name,
    f.forecast_step,
    f.point_forecast
FROM FORECAST(sales, 'date', 'sales', 'Theta', 12) f
JOIN products p ON f.product_id = p.product_id
GROUP BY p.product_id, p.product_name;

-- WITH CTEs
WITH recent_sales AS (
    SELECT * FROM sales WHERE date >= '2020-01-01'
)
SELECT * FROM FORECAST(recent_sales, 'date', 'sales', 'Theta', 12)
GROUP BY product_id;
```

### 4. DuckDB Query Optimization

```sql
-- DuckDB can optimize the entire query
SELECT 
    product_id,
    forecast_step,
    point_forecast
FROM FORECAST(sales, 'date', 'sales', 'Theta', 12)
WHERE sales.category = 'Electronics'  -- Filter pushed down
  AND sales.date >= '2020-01-01'      -- Filter pushed down
GROUP BY product_id
HAVING COUNT(*) >= 36                 -- Post-group filter
ORDER BY product_id, forecast_step;

-- DuckDB optimizes:
-- ✓ Filter pushdown (category, date)
-- ✓ Projection pushdown (only needed columns)
-- ✓ Parallel scan
-- ✓ Parallel hash aggregate
-- ✓ Post-aggregate filtering (HAVING)
-- ✓ Sorting
```

---

## Performance Comparison

### Table-In-Out vs Aggregate Functions

| Aspect | Aggregate Functions | **Table-In-Out Functions** | Winner |
|--------|---------------------|----------------------------|--------|
| **Output Format** | Struct (needs unnesting) | ✅ Direct table | v3 |
| **SQL Complexity** | Requires CTEs + unnest | ✅ Simple SELECT | v3 |
| **Parallelization** | GROUP BY | ✅ GROUP BY | Tie |
| **Performance** | Good | ✅ Better (no unnesting) | v3 |
| **Memory Usage** | Higher (structs) | ✅ Lower (streaming) | v3 |
| **Code Complexity** | Medium | ✅ Medium | Tie |
| **Composability** | Good | ✅ Excellent | v3 |

**Winner: Table-In-Out Functions** ✅

---

## Implementation Simplification

### Extension Registration (v3 - Clean!)

```cpp
void AnofoxTimeExtension::Load(DuckDB &db) {
    auto &catalog = Catalog::GetSystemCatalog(*db.instance);
    
    // ✅ Register table-in-out functions (main interface)
    catalog.CreateTableFunction("forecast", CreateForecastTableInOutFunction());
    catalog.CreateTableFunction("ensemble", CreateEnsembleTableInOutFunction());
    catalog.CreateTableFunction("backtest", CreateBacktestTableInOutFunction());
    
    // ✅ Register metric aggregates
    catalog.CreateAggregateFunction("ts_mae", CreateTSMaeAggregate());
    catalog.CreateAggregateFunction("ts_rmse", CreateTSRmseAggregate());
    catalog.CreateAggregateFunction("ts_mape", CreateTSMapeAggregate());
    
    // ✅ Register scalar utilities
    catalog.CreateScalarFunction("ts_detect_seasonality", DetectSeasonalityFunc());
    catalog.CreateScalarFunction("ts_detect_trend", DetectTrendFunc());
    
    // ✅ Optional: Specialized table functions
    catalog.CreateTableFunction("ts_decompose", DecomposeTableFunc());
}

// Clean and focused!
```

### What We Don't Need

```cpp
// ❌ OLD: Complex aggregate functions with structs
// ❌ OLD: Custom unnesting logic
// ❌ OLD: Complex CTE patterns
// ❌ OLD: Manual parallelization

// ✅ NEW: Simple table-in-out functions
// ✅ NEW: Direct table output
// ✅ NEW: Standard SQL patterns
// ✅ NEW: DuckDB parallelization
```

---

## Complete Example Workflows

### Workflow 1: E-commerce Batch Forecast

```sql
-- Forecast 50,000 products in parallel
SELECT 
    product_id,
    category,
    forecast_step,
    point_forecast,
    lower_95,
    upper_95,
    model_name,
    fit_time_ms
FROM FORECAST(
    product_sales,
    'date',
    'sales',
    'AutoETS',
    12,
    {'seasonal_period': 12}
)
GROUP BY product_id, category
ORDER BY product_id, forecast_step;

-- DuckDB automatically:
-- ✓ Partitions 50,000 products across cores
-- ✓ Processes each group in parallel
-- ✓ Returns tabular results directly
-- ✓ Optimizes memory usage
```

### Workflow 2: Financial Ensemble

```sql
-- Ensemble forecast for 5,000 assets
SELECT 
    asset_id,
    forecast_step,
    point_forecast,
    lower_95,
    upper_95
FROM ENSEMBLE(
    asset_prices,
    'timestamp',
    'price',
    ['SES', 'Theta', 'AutoETS', 'TBATS'],
    30,
    'WeightedAccuracy',
    {
        'accuracy_metric': 'MAE',
        'validation_split': 0.25,
        'temperature': 0.5
    }
)
GROUP BY asset_id
ORDER BY asset_id, forecast_step;

-- DuckDB parallelizes across assets automatically!
```

### Workflow 3: Hierarchical Rollup

```sql
-- Bottom-up forecasting with rollup
WITH sku_forecasts AS (
    SELECT 
        sku_id,
        store_id,
        forecast_step,
        point_forecast
    FROM FORECAST(sku_sales, 'date', 'units', 'Theta', 12)
    GROUP BY sku_id, store_id
),
store_forecasts AS (
    SELECT 
        store_id,
        forecast_step,
        SUM(point_forecast) as total_forecast
    FROM sku_forecasts
    GROUP BY store_id, forecast_step
)
SELECT 
    store_id,
    forecast_step,
    total_forecast,
    RANK() OVER (PARTITION BY forecast_step ORDER BY total_forecast DESC) as rank
FROM store_forecasts
ORDER BY forecast_step, rank;
```

---

## Migration from Previous Designs

### From v1 (Custom Table Functions)

```sql
-- OLD v1:
SELECT * FROM FORECAST_BATCH(
    'sales', 'date', 'sales', ['product_id'], 'Theta', 12,
    parallel_tasks := 16
);

-- ✅ NEW v3:
SELECT * FROM FORECAST(sales, 'date', 'sales', 'Theta', 12)
GROUP BY product_id;
```

### From v2 (Aggregate Functions)

```sql
-- OLD v2 (requires unnesting):
WITH forecasts AS (
    SELECT 
        product_id,
        TS_FORECAST(date, sales, 'Theta', 12) as f
    FROM sales
    GROUP BY product_id
)
SELECT 
    product_id,
    unnest(generate_series(1, 12)) as forecast_step,
    unnest(f.point) as point_forecast
FROM forecasts;

-- ✅ NEW v3 (direct table output):
SELECT 
    product_id,
    forecast_step,
    point_forecast
FROM FORECAST(sales, 'date', 'sales', 'Theta', 12)
GROUP BY product_id;
```

---

## Summary

### Why Table-In-Out Functions are Perfect

1. **✅ Leverages DuckDB's GROUP BY parallelization** (automatic)
2. **✅ Returns tabular output directly** (no unnesting)
3. **✅ Uses standard SQL patterns** (familiar to users)
4. **✅ Optimized by DuckDB** (query planner works)
5. **✅ Clean implementation** (less code than aggregates)
6. **✅ Best performance** (no struct overhead)

### Implementation Priority

**Phase 1 (Weeks 1-2):**
- [ ] FORECAST() table-in-out function
- [ ] Basic model factory (Naive, SES, Theta)
- [ ] Time series builder
- [ ] Tests with GROUP BY

**Phase 2 (Weeks 3-4):**
- [ ] ENSEMBLE() table-in-out function
- [ ] All ensemble methods (Mean, Median, Weighted)
- [ ] Extended model factory (all 30+ models)

**Phase 3 (Weeks 5-6):**
- [ ] BACKTEST() table-in-out function
- [ ] Metric aggregates (MAE, RMSE, etc.)
- [ ] Performance optimization

**Phase 4 (Weeks 7-8):**
- [ ] Scalar functions (detect_seasonality, etc.)
- [ ] Specialized table functions (decompose)
- [ ] Documentation and examples

**Total: 8 weeks** (same as v2, but better output format)

---

## Conclusion

**Table-in-out functions are the perfect solution!**

They combine:
- ✅ **DuckDB's parallel GROUP BY** (automatic parallelization)
- ✅ **Direct table output** (no unnesting needed)
- ✅ **Standard SQL syntax** (familiar patterns)
- ✅ **Optimal performance** (DuckDB optimized)
- ✅ **Clean implementation** (less complex than aggregates)

**Recommendation: Implement v3 design using table-in-out functions.**

---

**Files to reference:**
- **This document**: `DUCKDB_EXTENSION_DESIGN_V3.md` (final design)
- **Comparison**: `DUCKDB_DESIGN_COMPARISON.md` (why v3 wins)
- **DuckDB Interface**: [table_in_out_function](https://github.com/duckdb/duckdb/blob/56cfbb0c2b53fa84b4eb09fb21034357a68195fd/src/include/duckdb/function/table_function.hpp#L283)

**Next steps:**
1. Review v3 design
2. Start with FORECAST() table-in-out function
3. Test with GROUP BY on sample data
4. Expand to full model set + ensembles
5. Deploy!

**This is the optimal design for the DuckDB extension!** 🎉
