# Using anofox-forecast from Rust

## Overview

Use anofox-forecast from Rust through the DuckDB Rust client. Combines Rust's safety guarantees with DuckDB's analytical power.

**Key Advantages**:
- ✅ Memory safety + zero-cost abstractions
- ✅ Type-safe query results
- ✅ Fearless concurrency
- ✅ Easy error handling (Result type)
- ✅ Perfect for production services

## Installation

Add to `Cargo.toml`:

```toml
[dependencies]
duckdb = "1.1"
serde = { version = "1.0", features = ["derive"] }
serde_json = "1.0"
chrono = "0.4"  # For date/time handling
```

## Quick Start

```rust
use duckdb::{Connection, Result};

fn main() -> Result<()> {
    // Connect to DuckDB
    let conn = Connection::open_in_memory()?;
    
    // Load extension
    conn.execute_batch(
        "LOAD 'path/to/anofox_forecast.duckdb_extension'"
    )?;
    
    // Create sample data
    conn.execute_batch(r#"
        CREATE TABLE sales AS
        SELECT 
            DATE '2023-01-01' + INTERVAL (d) DAY AS date,
            100 + 20 * SIN(2 * PI() * d / 7) + random() * 10 AS amount
        FROM generate_series(0, 89) t(d)
    "#)?;
    
    // Generate forecast
    let mut stmt = conn.prepare(r#"
        SELECT forecast_step, date_col, point_forecast, lower, upper
        FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, 
                         {'seasonal_period': 7})
    "#)?;
    
    let forecast_iter = stmt.query_map([], |row| {
        Ok(ForecastPoint {
            step: row.get(0)?,
            date: row.get::<_, String>(1)?,
            forecast: row.get(2)?,
            lower: row.get(3)?,
            upper: row.get(4)?,
        })
    })?;
    
    // Print results
    for (i, point) in forecast_iter.enumerate() {
        let p = point?;
        println!("Step {}: {} = {:.2}", p.step, p.date, p.forecast);
        if i >= 4 { break; }  // Print first 5
    }
    
    Ok(())
}

#[derive(Debug)]
struct ForecastPoint {
    step: i32,
    date: String,
    forecast: f64,
    lower: f64,
    upper: f64,
}
```

## Type-Safe Structures

```rust
use duckdb::{Connection, Result};
use serde::{Deserialize, Serialize};

#[derive(Debug, Serialize, Deserialize)]
struct ForecastPoint {
    forecast_step: i32,
    date_col: String,
    point_forecast: f64,
    lower: f64,
    upper: f64,
    model_name: String,
    confidence_level: f64,
}

#[derive(Debug, Serialize)]
struct SeriesStats {
    series_id: String,
    length: i64,
    mean: f64,
    std: f64,
    quality_score: f64,
    n_gaps: i64,
    n_null: i64,
}

fn get_forecast(conn: &Connection, product_id: &str, horizon: i32) -> Result<Vec<ForecastPoint>> {
    let query = format!(r#"
        SELECT * FROM TS_FORECAST(
            (SELECT * FROM sales WHERE product_id = '{}'),
            date, amount, 'AutoETS', {}, {{'seasonal_period': 7}}
        )
    "#, product_id, horizon);
    
    let mut stmt = conn.prepare(&query)?;
    let forecast_iter = stmt.query_map([], |row| {
        Ok(ForecastPoint {
            forecast_step: row.get(0)?,
            date_col: row.get(1)?,
            point_forecast: row.get(2)?,
            lower: row.get(3)?,
            upper: row.get(4)?,
            model_name: row.get(5)?,
            confidence_level: row.get(7)?,
        })
    })?;
    
    forecast_iter.collect()
}

fn main() -> Result<()> {
    let conn = Connection::open_in_memory()?;
    conn.execute_batch("LOAD 'anofox_forecast.duckdb_extension'")?;
    
    let forecasts = get_forecast(&conn, "P001", 28)?;
    
    println!("Generated {} forecasts", forecasts.len());
    println!("First forecast: {:.2}", forecasts[0].point_forecast);
    
    Ok(())
}
```

## Data Preparation

```rust
use duckdb::{Connection, Result};

fn prepare_data(conn: &Connection) -> Result<()> {
    conn.execute_batch("LOAD 'anofox_forecast.duckdb_extension'")?;
    
    // Load raw data
    conn.execute_batch("CREATE TABLE sales_raw AS SELECT * FROM read_csv('sales.csv')")?;
    
    // Analyze quality
    let mut stmt = conn.prepare(r#"
        SELECT COUNT(*) as n_series, AVG(quality_score) as avg_quality
        FROM TS_STATS('sales_raw', product_id, date, amount)
    "#)?;
    
    let (n_series, avg_quality): (i64, f64) = stmt.query_row([], |row| {
        Ok((row.get(0)?, row.get(1)?))
    })?;
    
    println!("Data quality: {:.3} for {} series", avg_quality, n_series);
    
    // Prepare pipeline
    conn.execute_batch(r#"
        CREATE TABLE sales_prepared AS
        WITH filled AS (
            SELECT * FROM TS_FILL_GAPS('sales_raw', product_id, date, amount)
        ),
        cleaned AS (
            SELECT * FROM TS_DROP_CONSTANT('filled', product_id, amount)
        )
        SELECT * FROM TS_FILL_NULLS_FORWARD('cleaned', product_id, date, amount)
    "#)?;
    
    println!("Data prepared successfully");
    
    Ok(())
}

fn main() -> Result<()> {
    let conn = Connection::open("warehouse.duckdb")?;
    prepare_data(&conn)?;
    Ok(())
}
```

## Web Service with Actix-Web

```rust
use actix_web::{get, web, App, HttpResponse, HttpServer};
use duckdb::{Connection, Result as DuckDBResult};
use serde::{Deserialize, Serialize};
use std::sync::Mutex;

struct AppState {
    conn: Mutex<Connection>,
}

#[derive(Serialize)]
struct ForecastResponse {
    product_id: String,
    forecasts: Vec<ForecastPoint>,
}

#[derive(Serialize)]
struct ForecastPoint {
    step: i32,
    date: String,
    forecast: f64,
    lower: f64,
    upper: f64,
}

#[derive(Deserialize)]
struct ForecastQuery {
    horizon: Option<i32>,
}

#[get("/forecast/{product_id}")]
async fn forecast_handler(
    product_id: web::Path<String>,
    query: web::Query<ForecastQuery>,
    data: web::Data<AppState>,
) -> HttpResponse {
    let horizon = query.horizon.unwrap_or(28);
    
    let conn = data.conn.lock().unwrap();
    
    let sql = format!(r#"
        SELECT forecast_step, date_col::VARCHAR, point_forecast, lower, upper
        FROM TS_FORECAST(
            (SELECT * FROM sales WHERE product_id = '{}'),
            date, amount, 'AutoETS', {}, {{'seasonal_period': 7}}
        )
    "#, product_id, horizon);
    
    let mut stmt = match conn.prepare(&sql) {
        Ok(s) => s,
        Err(e) => return HttpResponse::InternalServerError().body(format!("Error: {}", e)),
    };
    
    let forecasts: Vec<ForecastPoint> = stmt.query_map([], |row| {
        Ok(ForecastPoint {
            step: row.get(0)?,
            date: row.get(1)?,
            forecast: row.get(2)?,
            lower: row.get(3)?,
            upper: row.get(4)?,
        })
    }).unwrap().map(|r| r.unwrap()).collect();
    
    let response = ForecastResponse {
        product_id: product_id.to_string(),
        forecasts,
    };
    
    HttpResponse::Ok().json(response)
}

#[actix_web::main]
async fn main() -> std::io::Result<()> {
    let conn = Connection::open("warehouse.duckdb").unwrap();
    conn.execute_batch("LOAD 'anofox_forecast.duckdb_extension'").unwrap();
    
    let app_state = web::Data::new(AppState {
        conn: Mutex::new(conn),
    });
    
    println!("Server running on http://localhost:8080");
    
    HttpServer::new(move || {
        App::new()
            .app_data(app_state.clone())
            .service(forecast_handler)
    })
    .bind(("0.0.0.0", 8080))?
    .run()
    .await
}
```

## Error Handling

```rust
use duckdb::{Connection, Result, Error};
use thiserror::Error;

#[derive(Error, Debug)]
enum ForecastError {
    #[error("Database error: {0}")]
    Database(#[from] duckdb::Error),
    
    #[error("Insufficient data for product {0}")]
    InsufficientData(String),
    
    #[error("Constant series for product {0}")]
    ConstantSeries(String),
    
    #[error("Unknown error for product {0}: {1}")]
    Unknown(String, String),
}

fn safe_forecast(conn: &Connection, product_id: &str) -> Result<Vec<ForecastPoint>, ForecastError> {
    let query = format!(r#"
        SELECT forecast_step, date_col::VARCHAR, point_forecast, lower, upper
        FROM TS_FORECAST(
            (SELECT * FROM sales WHERE product_id = '{}'),
            date, amount, 'AutoETS', 28, {{'seasonal_period': 7}}
        )
    "#, product_id);
    
    let mut stmt = conn.prepare(&query)
        .map_err(|e| {
            let err_msg = format!("{}", e);
            if err_msg.contains("too short") {
                ForecastError::InsufficientData(product_id.to_string())
            } else if err_msg.contains("constant") {
                ForecastError::ConstantSeries(product_id.to_string())
            } else {
                ForecastError::Unknown(product_id.to_string(), err_msg)
            }
        })?;
    
    let forecasts: Vec<ForecastPoint> = stmt.query_map([], |row| {
        Ok(ForecastPoint {
            step: row.get(0)?,
            date: row.get(1)?,
            forecast: row.get(2)?,
            lower: row.get(3)?,
            upper: row.get(4)?,
        })
    })?.map(|r| r.unwrap()).collect();
    
    Ok(forecasts)
}

fn main() {
    let conn = Connection::open_in_memory().unwrap();
    conn.execute_batch("LOAD 'anofox_forecast.duckdb_extension'").unwrap();
    
    let products = vec!["P001", "P002", "P003"];
    
    for pid in products {
        match safe_forecast(&conn, pid) {
            Ok(fc) => println!("{}: {} forecasts", pid, fc.len()),
            Err(ForecastError::InsufficientData(p)) => {
                eprintln!("{}: Insufficient data", p);
            }
            Err(e) => eprintln!("Error: {}", e),
        }
    }
}
```

## Async I/O with Tokio

```rust
use duckdb::{Connection, Result};
use tokio;

#[tokio::main]
async fn main() -> Result<()> {
    // DuckDB operations on background thread
    let forecasts = tokio::task::spawn_blocking(|| {
        let conn = Connection::open("warehouse.duckdb")?;
        conn.execute_batch("LOAD 'anofox_forecast.duckdb_extension'")?;
        
        let mut stmt = conn.prepare(r#"
            SELECT * FROM TS_FORECAST_BY('sales', product_id, date, amount,
                                         'AutoETS', 28, {'seasonal_period': 7})
        "#)?;
        
        let count = stmt.query_map([], |row| {
            Ok(())  // Just count
        })?.count();
        
        Ok::<_, duckdb::Error>(count)
    }).await.unwrap()?;
    
    println!("Generated {} forecast points", forecasts);
    
    Ok(())
}
```

## CLI Application

```rust
use duckdb::{Connection, Result};
use clap::{Parser, Subcommand};

#[derive(Parser)]
#[command(name = "forecast-cli")]
#[command(about = "Time series forecasting CLI", long_about = None)]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    /// Generate forecasts
    Forecast {
        #[arg(short, long)]
        product: String,
        
        #[arg(short, long, default_value_t = 28)]
        horizon: i32,
        
        #[arg(short, long, default_value = "AutoETS")]
        model: String,
    },
    
    /// Check data quality
    Quality {
        #[arg(short, long)]
        product: String,
    },
}

fn main() -> Result<()> {
    let cli = Cli::parse();
    
    let conn = Connection::open("warehouse.duckdb")?;
    conn.execute_batch("LOAD 'anofox_forecast.duckdb_extension'")?;
    
    match &cli.command {
        Commands::Forecast { product, horizon, model } => {
            let query = format!(r#"
                SELECT * FROM TS_FORECAST(
                    (SELECT * FROM sales WHERE product_id = '{}'),
                    date, amount, '{}', {}, {{'seasonal_period': 7}}
                )
            "#, product, model, horizon);
            
            let mut stmt = conn.prepare(&query)?;
            let mut rows = stmt.query([])?;
            
            println!("Forecast for {}", product);
            while let Some(row) = rows.next()? {
                let step: i32 = row.get(0)?;
                let forecast: f64 = row.get(2)?;
                println!("  Step {}: {:.2}", step, forecast);
            }
        }
        
        Commands::Quality { product } => {
            let query = format!(r#"
                SELECT * FROM TS_STATS(
                    (SELECT * FROM sales WHERE product_id = '{}'),
                    product_id, date, amount
                )
            "#, product);
            
            let mut stmt = conn.prepare(&query)?;
            let stats: (i64, f64, i64, i64, f64) = stmt.query_row([], |row| {
                Ok((
                    row.get::<_, i64>(1)?,    // length
                    row.get::<_, f64>(6)?,    // mean
                    row.get::<_, i64>(11)?,   // n_null
                    row.get::<_, i64>(3)?,    // n_gaps
                    row.get::<_, f64>(18)?,   // quality_score
                ))
            })?;
            
            println!("Quality stats for {}", product);
            println!("  Length: {}", stats.0);
            println!("  Mean: {:.2}", stats.1);
            println!("  Nulls: {}", stats.2);
            println!("  Gaps: {}", stats.3);
            println!("  Quality score: {:.3}", stats.4);
        }
    }
    
    Ok(())
}

// Usage:
// cargo run -- forecast --product P001 --horizon 28
// cargo run -- quality --product P001
```

## Concurrent Forecasting

```rust
use duckdb::{Connection, Result};
use rayon::prelude::*;

fn forecast_batch(products: &[String]) -> Result<()> {
    // DuckDB handles parallelization internally
    // Don't need rayon for GROUP BY queries
    let conn = Connection::open("warehouse.duckdb")?;
    conn.execute_batch("LOAD 'anofox_forecast.duckdb_extension'")?;
    
    let product_list: Vec<String> = products.iter()
        .map(|p| format!("'{}'", p))
        .collect();
    
    let query = format!(r#"
        CREATE TABLE forecasts AS
        SELECT * FROM TS_FORECAST_BY(
            (SELECT * FROM sales WHERE product_id IN ({})),
            product_id, date, amount, 'AutoETS', 28, {{'seasonal_period': 7}}
        )
    "#, product_list.join(", "));
    
    conn.execute_batch(&query)?;
    
    println!("Forecasted {} products", products.len());
    
    Ok(())
}

fn main() -> Result<()> {
    let products = vec![
        "P001".to_string(), "P002".to_string(), "P003".to_string(),
    ];
    
    forecast_batch(&products)?;
    
    Ok(())
}
```

## JSON Export

```rust
use duckdb::{Connection, Result};
use serde_json::Value;
use std::fs::File;
use std::io::Write;

fn export_forecast_json(conn: &Connection, output_path: &str) -> Result<()> {
    // Get forecast as JSON
    let query = r#"
        SELECT json_object(
            'product_id', product_id,
            'forecast_date', date_col::VARCHAR,
            'point_forecast', point_forecast,
            'lower_bound', lower,
            'upper_bound', upper,
            'model', model_name
        ) as forecast_json
        FROM TS_FORECAST_BY('sales', product_id, date, amount,
                            'AutoETS', 28, {'seasonal_period': 7})
    "#;
    
    let mut stmt = conn.prepare(query)?;
    let json_iter = stmt.query_map([], |row| {
        let json_str: String = row.get(0)?;
        Ok(json_str)
    })?;
    
    let mut file = File::create(output_path).unwrap();
    writeln!(file, "[").unwrap();
    
    for (i, json_result) in json_iter.enumerate() {
        let json_str = json_result?;
        if i > 0 {
            writeln!(file, ",").unwrap();
        }
        write!(file, "  {}", json_str).unwrap();
    }
    
    writeln!(file, "\n]").unwrap();
    
    println!("Exported forecasts to {}", output_path);
    
    Ok(())
}
```

## Evaluation Metrics

```rust
use duckdb::{Connection, Result};

#[derive(Debug)]
struct ForecastMetrics {
    product_id: String,
    mae: f64,
    rmse: f64,
    mape: f64,
    coverage: f64,
}

fn evaluate_forecasts(conn: &Connection) -> Result<Vec<ForecastMetrics>> {
    let query = r#"
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
    "#;
    
    let mut stmt = conn.prepare(query)?;
    let metrics_iter = stmt.query_map([], |row| {
        Ok(ForecastMetrics {
            product_id: row.get(0)?,
            mae: row.get(1)?,
            rmse: row.get(2)?,
            mape: row.get(3)?,
            coverage: row.get(4)?,
        })
    })?;
    
    metrics_iter.collect()
}

fn main() -> Result<()> {
    let conn = Connection::open_in_memory()?;
    conn.execute_batch("LOAD 'anofox_forecast.duckdb_extension'")?;
    
    let metrics = evaluate_forecasts(&conn)?;
    
    for m in metrics {
        println!("{}: MAE={:.2}, MAPE={:.2}%, Coverage={:.1}%", 
                 m.product_id, m.mae, m.mape, m.coverage * 100.0);
    }
    
    Ok(())
}
```

## Best Practices

### 1. Connection Pool (for servers)

```rust
use duckdb::{Connection, Result};
use std::sync::Arc;
use parking_lot::Mutex;

struct ConnectionPool {
    connections: Vec<Arc<Mutex<Connection>>>,
    current: Mutex<usize>,
}

impl ConnectionPool {
    fn new(db_path: &str, pool_size: usize) -> Result<Self> {
        let mut connections = Vec::new();
        
        for _ in 0..pool_size {
            let conn = Connection::open(db_path)?;
            conn.execute_batch("LOAD 'anofox_forecast.duckdb_extension'")?;
            connections.push(Arc::new(Mutex::new(conn)));
        }
        
        Ok(Self {
            connections,
            current: Mutex::new(0),
        })
    }
    
    fn get(&self) -> Arc<Mutex<Connection>> {
        let mut idx = self.current.lock();
        let conn = self.connections[*idx].clone();
        *idx = (*idx + 1) % self.connections.len();
        conn
    }
}

// Usage in web server
let pool = Arc::new(ConnectionPool::new("warehouse.duckdb", 4).unwrap());

// In handler
let conn = pool.get();
let guard = conn.lock();
let result = guard.prepare(...)?;
```

### 2. Prepared Statement Cache

```rust
use duckdb::{Connection, Result, Statement};
use std::collections::HashMap;
use std::sync::Mutex;

struct StatementCache {
    conn: Connection,
    cache: Mutex<HashMap<String, String>>,
}

impl StatementCache {
    fn new(db_path: &str) -> Result<Self> {
        let conn = Connection::open(db_path)?;
        conn.execute_batch("LOAD 'anofox_forecast.duckdb_extension'")?;
        
        Ok(Self {
            conn,
            cache: Mutex::new(HashMap::new()),
        })
    }
    
    fn forecast(&self, product_id: &str, horizon: i32) -> Result<Vec<ForecastPoint>> {
        let query = format!(r#"
            SELECT * FROM TS_FORECAST(
                (SELECT * FROM sales WHERE product_id = '{}'),
                date, amount, 'AutoETS', {}, {{'seasonal_period': 7}}
            )
        "#, product_id, horizon);
        
        // Execute (DuckDB caches query plans internally)
        let mut stmt = self.conn.prepare(&query)?;
        
        let forecasts: Vec<ForecastPoint> = stmt.query_map([], |row| {
            Ok(ForecastPoint {
                step: row.get(0)?,
                date: row.get(1)?,
                forecast: row.get(2)?,
                lower: row.get(3)?,
                upper: row.get(4)?,
            })
        })?.map(|r| r.unwrap()).collect();
        
        Ok(forecasts)
    }
}
```

## Summary

**Why Use from Rust?**
- ✅ Maximum safety (no segfaults, no data races)
- ✅ Zero-cost abstractions
- ✅ Excellent error handling (Result type)
- ✅ Perfect for production services
- ✅ Great async support (Tokio)

**Typical Rust Workflow**:
```
Load data → Query → Type-safe results → Process → Output
```

**Performance**:
- Lowest latency (comparable to C++)
- Memory safe (no undefined behavior)
- Thread safe (fearless concurrency)
- Production-grade reliability

**Use Cases**:
- High-performance web services (Actix, Axum)
- CLI tools (Clap)
- Embedded forecasting in Rust applications
- Microservices with strict latency requirements
- Systems where safety is critical

---

**Next**: [Python Usage Guide](81_python_integration.md) | [C++ Usage Guide](84_cpp_integration.md)

**Rust + DuckDB**: Safe, fast, and production-ready forecasting! 🦀

