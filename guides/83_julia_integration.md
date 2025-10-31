# Using anofox-forecast from Julia

## Overview

Use anofox-forecast from Julia through DuckDB.jl. Perfect for high-performance scientific computing with production-grade forecasting.

**Key Advantages**:
- ✅ Julia's speed + DuckDB's analytical power
- ✅ No Julia forecasting packages needed
- ✅ Easy DataFrame integration
- ✅ Type-safe queries
- ✅ REPL-friendly

## Installation

```julia
using Pkg
Pkg.add("DuckDB")
Pkg.add("DataFrames")  # Optional, for DataFrame support
```

## Quick Start

```julia
using DuckDB
using DataFrames

# Connect to DuckDB
con = DBInterface.connect(DuckDB.DB)

# Load extension
DBInterface.execute(con, "LOAD 'path/to/anofox_forecast.duckdb_extension'")

# Create sample data
DBInterface.execute(con, """
    CREATE TABLE sales AS
    SELECT 
        DATE '2023-01-01' + INTERVAL (d) DAY AS date,
        100 + 20 * SIN(2 * PI() * d / 7) + random() * 10 AS amount
    FROM generate_series(0, 89) t(d)
""")

# Generate forecast
forecast = DataFrame(DBInterface.execute(con, """
    SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, 
                              {'seasonal_period': 7})
"""))

# View results
println(first(forecast, 5))
println("Average forecast: ", round(mean(forecast.point_forecast), digits=2))
```

## Working with DataFrames

### Load Data from Julia DataFrame

```julia
using DuckDB
using DataFrames
using Dates

# Create sample DataFrame
sales = DataFrame(
    date = Date(2023,1,1):Day(1):Date(2023,3,31),
    amount = 100 .+ 20 .* sin.(2π .* (0:89) ./ 7) .+ randn(90) .* 5
)

# Connect and register DataFrame
con = DBInterface.connect(DuckDB.DB)
DBInterface.execute(con, "LOAD 'anofox_forecast.duckdb_extension'")
DuckDB.register_data_frame(con, sales, "sales")

# Forecast
forecast = DataFrame(DBInterface.execute(con, """
    SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28,
                              {'seasonal_period': 7})
"""))

println(forecast)
```

### Multiple Series

```julia
using DuckDB
using DataFrames

# Multi-series data
products = repeat(["P1", "P2", "P3"], inner=90)
dates = repeat(Date(2023,1,1):Day(1):Date(2023,3,31), 3)
amounts = vcat([100 .+ i*20 .+ 30*sin.(2π .* (0:89) ./ 7) .+ randn(90)*5 for i in 1:3]...)

sales = DataFrame(
    product_id = products,
    date = dates,
    amount = amounts
)

con = DBInterface.connect(DuckDB.DB)
DBInterface.execute(con, "LOAD 'anofox_forecast.duckdb_extension'")
DuckDB.register_data_frame(con, sales, "sales")

# Forecast all products in parallel
forecasts = DataFrame(DBInterface.execute(con, """
    SELECT * FROM TS_FORECAST_BY('sales', product_id, date, amount,
                                 'AutoETS', 14, {'seasonal_period': 7})
"""))

# Group by product
by_product = groupby(forecasts, :product_id)
for group in by_product
    println("Product $(group[1, :product_id]): mean forecast = $(round(mean(group.point_forecast), digits=2))")
end
```

## Data Preparation

```julia
using DuckDB
using DataFrames

con = DBInterface.connect(DuckDB.DB)
DBInterface.execute(con, "LOAD 'anofox_forecast.duckdb_extension'")

# Load data
DBInterface.execute(con, "CREATE TABLE sales_raw AS SELECT * FROM read_csv('sales.csv')")

# Analyze quality
stats = DataFrame(DBInterface.execute(con, """
    SELECT * FROM TS_STATS('sales_raw', product_id, date, amount)
"""))

println("Quality report:")
println("  Average quality score: ", round(mean(stats.quality_score), digits=3))
println("  Series with issues: ", count(stats.quality_score .< 0.7))

# Prepare data
DBInterface.execute(con, """
    CREATE TABLE sales_prepared AS
    WITH filled AS (
        SELECT * FROM TS_FILL_GAPS('sales_raw', product_id, date, amount)
    ),
    cleaned AS (
        SELECT * FROM TS_DROP_CONSTANT('filled', product_id, amount)
    )
    SELECT * FROM TS_FILL_NULLS_FORWARD('cleaned', product_id, date, amount)
""")

# Forecast
forecasts = DataFrame(DBInterface.execute(con, """
    SELECT * FROM TS_FORECAST_BY('sales_prepared', product_id, date, amount,
                                 'AutoETS', 28, {'seasonal_period': 7})
"""))
```

## Visualization with Plots.jl

```julia
using DuckDB
using DataFrames
using Plots

con = DBInterface.connect(DuckDB.DB)
DBInterface.execute(con, "LOAD 'anofox_forecast.duckdb_extension'")

# Get historical data
historical = DataFrame(DBInterface.execute(con, """
    SELECT date, amount FROM sales ORDER BY date
"""))

# Get forecast
forecast = DataFrame(DBInterface.execute(con, """
    SELECT date_col, point_forecast, lower, upper
    FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28,
                     {'seasonal_period': 7, 'confidence_level': 0.95})
"""))

# Plot
plot(historical.date, historical.amount, 
     label="Historical", linewidth=2, color=:black)
plot!(forecast.date_col, forecast.point_forecast, 
     label="Forecast", linewidth=2, color=:blue)
plot!(forecast.date_col, [forecast.lower forecast.upper],
     fillrange=[forecast.lower, forecast.upper],
     fillalpha=0.3, color=:blue, label="95% CI")
xlabel!("Date")
ylabel!("Amount")
title!("Sales Forecast")
savefig("forecast.png")
```

## Performance Optimization

### Persistent Connection

```julia
using DuckDB
using DataFrames

# Global connection (reuse across functions)
const CON = Ref{DuckDB.DB}()

function init_connection()
    CON[] = DBInterface.connect(DuckDB.DB, "warehouse.duckdb")
    DBInterface.execute(CON[], "LOAD 'anofox_forecast.duckdb_extension'")
    @info "Connection initialized"
end

function forecast_product(product_id::String, horizon::Int=28)
    result = DataFrame(DBInterface.execute(CON[], """
        SELECT * FROM TS_FORECAST(
            (SELECT * FROM sales WHERE product_id = '$product_id'),
            date, amount, 'AutoETS', $horizon, {'seasonal_period': 7}
        )
    """))
    return result
end

# Initialize once
init_connection()

# Use multiple times (fast!)
fc1 = forecast_product("P001")
fc2 = forecast_product("P002", 14)
fc3 = forecast_product("P003", 60)
```

### Parallel Processing

```julia
using DuckDB
using DataFrames
using Base.Threads

con = DBInterface.connect(DuckDB.DB)
DBInterface.execute(con, "LOAD 'anofox_forecast.duckdb_extension'")

# Get products
products = ["P001", "P002", "P003", "P004", "P005"]

# DuckDB handles parallelization internally for GROUP BY
# Just use TS_FORECAST_BY - no need for Julia threading
forecasts = DataFrame(DBInterface.execute(con, """
    SELECT * FROM TS_FORECAST_BY(
        (SELECT * FROM sales WHERE product_id IN ('P001','P002','P003','P004','P005')),
        product_id, date, amount, 'AutoETS', 28, {'seasonal_period': 7}
    )
"""))

println("Forecasted $(length(unique(forecasts.product_id))) products in parallel")
```

## Evaluation

```julia
using DuckDB
using DataFrames

con = DBInterface.connect(DuckDB.DB)
DBInterface.execute(con, "LOAD 'anofox_forecast.duckdb_extension'")

# Register data
DuckDB.register_data_frame(con, actuals_df, "actuals")
DuckDB.register_data_frame(con, forecasts_df, "forecasts")

# Compute metrics
metrics = DataFrame(DBInterface.execute(con, """
    WITH joined AS (
        SELECT 
            f.product_id,
            LIST(a.actual ORDER BY a.date) AS actuals,
            LIST(f.point_forecast ORDER BY f.forecast_step) AS forecasts,
            LIST(f.lower ORDER BY f.forecast_step) AS lower,
            LIST(f.upper ORDER BY f.forecast_step) AS upper
        FROM forecasts f
        JOIN actuals a ON f.product_id = a.product_id AND f.date_col = a.date
        GROUP BY f.product_id
    )
    SELECT 
        product_id,
        TS_MAE(actuals, forecasts) AS mae,
        TS_RMSE(actuals, forecasts) AS rmse,
        TS_MAPE(actuals, forecasts) AS mape,
        TS_COVERAGE(actuals, lower, upper) AS coverage
    FROM joined
"""))

println(metrics)

# Summary statistics
println("\nSummary:")
println("  Mean MAPE: ", round(mean(metrics.mape), digits=2), "%")
println("  Mean Coverage: ", round(mean(metrics.coverage)*100, digits=1), "%")
```

## Package Integration

### With CSV.jl

```julia
using DuckDB
using DataFrames
using CSV

# Read CSV with Julia
sales_df = CSV.read("sales.csv", DataFrame)

# Forecast with DuckDB
con = DBInterface.connect(DuckDB.DB)
DBInterface.execute(con, "LOAD 'anofox_forecast.duckdb_extension'")
DuckDB.register_data_frame(con, sales_df, "sales")

forecast = DataFrame(DBInterface.execute(con, """
    SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, 
                              {'seasonal_period': 7})
"""))

# Write back with CSV.jl
CSV.write("forecast.csv", forecast)
```

### With Arrow.jl

```julia
using DuckDB
using Arrow

# Read Parquet via Arrow
table = Arrow.Table("sales.parquet")

# Forecast with DuckDB
con = DBInterface.connect(DuckDB.DB)
DBInterface.execute(con, "LOAD 'anofox_forecast.duckdb_extension'")
DBInterface.execute(con, "CREATE TABLE sales AS SELECT * FROM table")

forecast = DataFrame(DBInterface.execute(con, """
    SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, 
                              {'seasonal_period': 7})
"""))

# Write as Arrow/Parquet
Arrow.write("forecast.arrow", forecast)
```

## Jupyter Notebook (IJulia)

```julia
# In Jupyter notebook

using DuckDB
using DataFrames
using Plots

con = DBInterface.connect(DuckDB.DB)
DBInterface.execute(con, "LOAD 'anofox_forecast.duckdb_extension'")

# Load data
sales = DataFrame(DBInterface.execute(con, 
    "SELECT * FROM read_csv('sales.csv')"))

# Register
DuckDB.register_data_frame(con, sales, "sales")

# Forecast
forecast = DataFrame(DBInterface.execute(con, """
    SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, 
                              {'seasonal_period': 7})
"""))

# Quick plot
plot(forecast.date_col, forecast.point_forecast, 
     label="Forecast", linewidth=2)
```

## Type Safety

```julia
using DuckDB
using DataFrames

# Define types for safety
struct ForecastResult
    forecast_step::Vector{Int32}
    date_col::Vector{Date}
    point_forecast::Vector{Float64}
    lower::Vector{Float64}
    upper::Vector{Float64}
    model_name::String
    confidence_level::Float64
end

function get_forecast(product_id::String, horizon::Int)::ForecastResult
    con = DBInterface.connect(DuckDB.DB)
    DBInterface.execute(con, "LOAD 'anofox_forecast.duckdb_extension'")
    
    df = DataFrame(DBInterface.execute(con, """
        SELECT * FROM TS_FORECAST(
            (SELECT * FROM sales WHERE product_id = '$product_id'),
            date, amount, 'AutoETS', $horizon, {'seasonal_period': 7}
        )
    """))
    
    return ForecastResult(
        df.forecast_step,
        Date.(df.date_col),
        df.point_forecast,
        df.lower,
        df.upper,
        df.model_name[1],
        df.confidence_level[1]
    )
end

# Usage with type checking
fc = get_forecast("P001", 28)
println(typeof(fc))  # ForecastResult
```

## Best Practices

### 1. Prepared Statements

```julia
using DuckDB

con = DBInterface.connect(DuckDB.DB)
DBInterface.execute(con, "LOAD 'anofox_forecast.duckdb_extension'")

# Prepare statement
stmt = DBInterface.prepare(con, """
    SELECT * FROM TS_FORECAST(
        (SELECT * FROM sales WHERE product_id = \$1),
        date, amount, 'AutoETS', \$2, {'seasonal_period': 7}
    )
""")

# Execute with different parameters
fc1 = DataFrame(DBInterface.execute(stmt, ["P001", 28]))
fc2 = DataFrame(DBInterface.execute(stmt, ["P002", 14]))
fc3 = DataFrame(DBInterface.execute(stmt, ["P003", 60]))
```

### 2. Error Handling

```julia
using DuckDB
using DataFrames

function safe_forecast(product_id::String, horizon::Int=28)
    try
        con = DBInterface.connect(DuckDB.DB)
        DBInterface.execute(con, "LOAD 'anofox_forecast.duckdb_extension'")
        
        result = DataFrame(DBInterface.execute(con, """
            SELECT * FROM TS_FORECAST(
                (SELECT * FROM sales WHERE product_id = '$product_id'),
                date, amount, 'AutoETS', $horizon, {'seasonal_period': 7}
            )
        """))
        
        return result
        
    catch e
        if occursin("too short", string(e))
            @warn "Product $product_id: Insufficient data"
        elseif occursin("constant", string(e))
            @warn "Product $product_id: Constant series"
        else
            @error "Product $product_id: $(string(e))"
        end
        return DataFrame()
    end
end
```

### 3. Logging

```julia
using DuckDB
using DataFrames
using Logging

function forecast_with_logging(table_name::String)
    @info "Starting forecast for $table_name"
    
    con = DBInterface.connect(DuckDB.DB)
    DBInterface.execute(con, "LOAD 'anofox_forecast.duckdb_extension'")
    
    # Check data quality
    stats = DataFrame(DBInterface.execute(con, """
        SELECT COUNT(*) as n_series, AVG(quality_score) as avg_quality
        FROM TS_STATS('$table_name', product_id, date, amount)
    """))
    
    @info "Data quality: $(round(stats.avg_quality[1], digits=3)) for $(stats.n_series[1]) series"
    
    # Forecast
    forecast = DataFrame(DBInterface.execute(con, """
        SELECT * FROM TS_FORECAST_BY('$table_name', product_id, date, amount,
                                     'AutoETS', 28, {'seasonal_period': 7})
    """))
    
    @info "Generated $(nrow(forecast)) forecast points"
    
    return forecast
end
```

## Integration Patterns

### Pattern 1: Data Pipeline

```julia
using DuckDB
using DataFrames
using CSV

function run_forecast_pipeline(input_file::String, output_file::String)
    @info "Starting pipeline"
    
    con = DBInterface.connect(DuckDB.DB)
    DBInterface.execute(con, "LOAD 'anofox_forecast.duckdb_extension'")
    
    # Load
    DBInterface.execute(con, "
        CREATE TABLE sales AS SELECT * FROM read_csv('$input_file')
    ")
    
    # Prepare
    DBInterface.execute(con, """
        CREATE TABLE sales_prep AS
        SELECT * FROM TS_FILL_GAPS('sales', product_id, date, amount)
    """)
    
    # Forecast
    forecasts = DataFrame(DBInterface.execute(con, """
        SELECT * FROM TS_FORECAST_BY('sales_prep', product_id, date, amount,
                                     'AutoETS', 28, {'seasonal_period': 7})
    """))
    
    # Save
    CSV.write(output_file, forecasts)
    
    @info "Pipeline complete: $(length(unique(forecasts.product_id))) products"
    
    return forecasts
end

# Run
forecasts = run_forecast_pipeline("sales.csv", "forecasts.csv")
```

### Pattern 2: HTTP API (Oxygen.jl)

```julia
using Oxygen
using DuckDB
using DataFrames
using JSON3

# Initialize connection
const con = DBInterface.connect(DuckDB.DB, "warehouse.duckdb")
DBInterface.execute(con, "LOAD 'anofox_forecast.duckdb_extension'")

@get "/forecast/{product_id}" function(req, product_id::String)
    horizon = get(req.query, "horizon", "28")
    
    result = DataFrame(DBInterface.execute(con, """
        SELECT * FROM TS_FORECAST(
            (SELECT * FROM sales WHERE product_id = '$product_id'),
            date, amount, 'AutoETS', $horizon, {'seasonal_period': 7}
        )
    """))
    
    return JSON3.write(result)
end

@get "/quality/{product_id}" function(req, product_id::String)
    stats = DataFrame(DBInterface.execute(con, """
        SELECT * FROM TS_STATS(
            (SELECT * FROM sales WHERE product_id = '$product_id'),
            product_id, date, amount
        )
    """))
    
    return JSON3.write(stats)
end

# Start server
serve(port=8080)
```

## Scientific Computing

### Numerical Analysis

```julia
using DuckDB
using DataFrames
using Statistics
using LinearAlgebra

con = DBInterface.connect(DuckDB.DB)
DBInterface.execute(con, "LOAD 'anofox_forecast.duckdb_extension'")

# Get in-sample fitted values
forecast = DataFrame(DBInterface.execute(con, """
    SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 7,
                              {'seasonal_period': 7, 'return_insample': true})
"""))

# Extract fitted values as Vector
fitted = forecast.insample_fitted[1]  # First row contains the array

# Get actuals
actuals = DataFrame(DBInterface.execute(con, 
    "SELECT amount FROM sales ORDER BY date")).amount

# Compute residuals
residuals = actuals .- fitted

# Statistical analysis
println("Residual analysis:")
println("  Mean: ", round(mean(residuals), digits=4))
println("  Std: ", round(std(residuals), digits=2))
println("  Min: ", round(minimum(residuals), digits=2))
println("  Max: ", round(maximum(residuals), digits=2))

# Autocorrelation of residuals
lag1_autocorr = cor(residuals[1:end-1], residuals[2:end])
println("  Lag-1 autocorr: ", round(lag1_autocorr, digits=4))

if abs(lag1_autocorr) < 0.2
    println("  ✓ Residuals appear random (good fit)")
else
    println("  ⚠ Residuals show pattern (model may miss something)")
end
```

## Summary

**Why Use from Julia?**
- ✅ Julia's performance + DuckDB's analytical power
- ✅ Type-safe forecasting workflows
- ✅ Easy DataFrame integration
- ✅ REPL-friendly for exploration
- ✅ Perfect for scientific computing pipelines

**Typical Julia Workflow**:
```
DataFrame → register → SQL forecast → DataFrame → analysis/plot
```

**Performance**:
- Single series: Similar to Python
- Multiple series: Excellent (DuckDB parallelization)
- Data prep: 3-4x faster than pure Julia for large datasets

**When to Use**:
- High-performance scientific computing
- Type-safe forecasting pipelines
- Integration with Julia's numerical ecosystem
- Building production APIs (Oxygen.jl)
- Research workflows with reproducibility

---

**Next**: [C++ Usage Guide](84_cpp_integration.md) | [Rust Usage Guide](85_rust_integration.md)

**Julia + DuckDB**: High-performance scientific forecasting! 🚀

