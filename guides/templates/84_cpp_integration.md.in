# Using anofox-forecast from C++

## Overview

Embed DuckDB with anofox-forecast extension in your C++ applications for high-performance, low-latency forecasting.

**Key Advantages**:
- ✅ Zero-overhead embedding
- ✅ No external process required
- ✅ Type-safe query results
- ✅ Full control over execution
- ✅ Perfect for low-latency applications

## Installation

### CMake Integration

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.15)
project(forecast_app)

set(CMAKE_CXX_STANDARD 17)

# Add DuckDB
include(FetchContent)
FetchContent_Declare(
    duckdb
    GIT_REPOSITORY https://github.com/duckdb/duckdb.git
    GIT_TAG v1.4.1
)
FetchContent_MakeAvailable(duckdb)

# Your executable
add_executable(forecast_app main.cpp)
target_link_libraries(forecast_app duckdb)
```

## Quick Start

```cpp
#include "duckdb.hpp"
#include <iostream>
#include <string>

int main() {
    // Connect to DuckDB
    duckdb::DuckDB db(nullptr);
    duckdb::Connection con(db);
    
    // Load extension
    con.Query("LOAD 'path/to/anofox_forecast.duckdb_extension'");
    
    // Create sample data
    con.Query(R"(
        CREATE TABLE sales AS
        SELECT 
            DATE '2023-01-01' + INTERVAL (d) DAY AS date,
            100 + 20 * SIN(2 * PI() * d / 7) + random() * 10 AS amount
        FROM generate_series(0, 89) t(d)
    )");
    
    // Generate forecast
    auto result = con.Query(R"(
        SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, 
                                  {'seasonal_period': 7})
    )");
    
    // Print results
    result->Print();
    
    // Access values
    auto &forecast_col = result->GetValue(2, 0);  // point_forecast column, first row
    std::cout << "First forecast: " << forecast_col.ToString() << std::endl;
    
    return 0;
}
```

## Working with Query Results

### Type-Safe Result Access

```cpp
#include "duckdb.hpp"
#include <vector>
#include <string>

struct ForecastPoint {
    int32_t forecast_step;
    std::string date;
    double point_forecast;
    double lower;
    double upper;
    std::string model_name;
};

std::vector<ForecastPoint> get_forecast(duckdb::Connection &con, 
                                        const std::string &product_id, 
                                        int horizon) {
    std::vector<ForecastPoint> forecasts;
    
    auto result = con.Query(
        "SELECT forecast_step, date_col::VARCHAR, point_forecast, lower, upper, model_name "
        "FROM TS_FORECAST("
        "  (SELECT * FROM sales WHERE product_id = '" + product_id + "'), "
        "  date, amount, 'AutoETS', " + std::to_string(horizon) + ", "
        "  {'seasonal_period': 7}"
        ")"
    );
    
    for (size_t row = 0; row < result->RowCount(); row++) {
        ForecastPoint fp;
        fp.forecast_step = result->GetValue(0, row).GetValue<int32_t>();
        fp.date = result->GetValue(1, row).GetValue<std::string>();
        fp.point_forecast = result->GetValue(2, row).GetValue<double>();
        fp.lower = result->GetValue(3, row).GetValue<double>();
        fp.upper = result->GetValue(4, row).GetValue<double>();
        fp.model_name = result->GetValue(5, row).GetValue<std::string>();
        forecasts.push_back(fp);
    }
    
    return forecasts;
}

int main() {
    duckdb::DuckDB db(nullptr);
    duckdb::Connection con(db);
    con.Query("LOAD 'anofox_forecast.duckdb_extension'");
    
    auto forecasts = get_forecast(con, "P001", 28);
    
    std::cout << "Forecasted " << forecasts.size() << " points" << std::endl;
    std::cout << "First: " << forecasts[0].point_forecast << std::endl;
    
    return 0;
}
```

### Prepared Statements

```cpp
#include "duckdb.hpp"

int main() {
    duckdb::DuckDB db(nullptr);
    duckdb::Connection con(db);
    con.Query("LOAD 'anofox_forecast.duckdb_extension'");
    
    // Prepare statement
    auto stmt = con.Prepare(R"(
        SELECT * FROM TS_FORECAST(
            (SELECT * FROM sales WHERE product_id = $1),
            date, amount, 'AutoETS', $2, {'seasonal_period': 7}
        )
    )");
    
    // Execute with parameters
    auto result1 = stmt->Execute("P001", 28);
    auto result2 = stmt->Execute("P002", 14);
    
    result1->Print();
    result2->Print();
    
    return 0;
}
```

## Real-World Application

### High-Performance Forecasting Service

```cpp
#include "duckdb.hpp"
#include "httplib.h"  // cpp-httplib for HTTP server
#include <nlohmann/json.hpp>
#include <memory>

class ForecastService {
private:
    std::unique_ptr<duckdb::DuckDB> db_;
    std::unique_ptr<duckdb::Connection> con_;
    
public:
    ForecastService(const std::string &db_path) {
        db_ = std::make_unique<duckdb::DuckDB>(db_path.c_str());
        con_ = std::make_unique<duckdb::Connection>(*db_);
        con_->Query("LOAD 'anofox_forecast.duckdb_extension'");
    }
    
    nlohmann::json forecast_product(const std::string &product_id, int horizon) {
        auto result = con_->Query(
            "SELECT forecast_step, date_col::VARCHAR AS date, "
            "       point_forecast, lower, upper "
            "FROM TS_FORECAST("
            "  (SELECT * FROM sales WHERE product_id = '" + product_id + "'), "
            "  date, amount, 'AutoETS', " + std::to_string(horizon) + ", "
            "  {'seasonal_period': 7}"
            ")"
        );
        
        nlohmann::json forecast_array = nlohmann::json::array();
        
        for (size_t row = 0; row < result->RowCount(); row++) {
            nlohmann::json point;
            point["step"] = result->GetValue(0, row).GetValue<int32_t>();
            point["date"] = result->GetValue(1, row).GetValue<std::string>();
            point["forecast"] = result->GetValue(2, row).GetValue<double>();
            point["lower"] = result->GetValue(3, row).GetValue<double>();
            point["upper"] = result->GetValue(4, row).GetValue<double>();
            forecast_array.push_back(point);
        }
        
        return forecast_array;
    }
    
    nlohmann::json get_stats(const std::string &product_id) {
        auto result = con_->Query(
            "SELECT series_id, length, quality_score, mean, std "
            "FROM TS_STATS("
            "  (SELECT * FROM sales WHERE product_id = '" + product_id + "'), "
            "  product_id, date, amount"
            ")"
        );
        
        nlohmann::json stats;
        if (result->RowCount() > 0) {
            stats["product_id"] = result->GetValue(0, 0).GetValue<std::string>();
            stats["length"] = result->GetValue(1, 0).GetValue<int64_t>();
            stats["quality_score"] = result->GetValue(2, 0).GetValue<double>();
            stats["mean"] = result->GetValue(3, 0).GetValue<double>();
            stats["std"] = result->GetValue(4, 0).GetValue<double>();
        }
        
        return stats;
    }
};

int main() {
    ForecastService service("warehouse.duckdb");
    
    httplib::Server svr;
    
    // Endpoint: GET /forecast/P001?horizon=28
    svr.Get(R"(/forecast/([^/]+))", [&](const httplib::Request &req, httplib::Response &res) {
        std::string product_id = req.matches[1];
        int horizon = std::stoi(req.get_param_value("horizon", "28"));
        
        auto forecast = service.forecast_product(product_id, horizon);
        res.set_content(forecast.dump(), "application/json");
    });
    
    // Endpoint: GET /quality/P001
    svr.Get(R"(/quality/([^/]+))", [&](const httplib::Request &req, httplib::Response &res) {
        std::string product_id = req.matches[1];
        auto stats = service.get_stats(product_id);
        res.set_content(stats.dump(), "application/json");
    });
    
    std::cout << "Server running on http://localhost:8080" << std::endl;
    svr.listen("0.0.0.0", 8080);
    
    return 0;
}
```

## Batch Processing

```cpp
#include "duckdb.hpp"
#include <vector>
#include <iostream>
#include <thread>

void process_batch(const std::string &db_path, 
                   const std::vector<std::string> &product_ids) {
    duckdb::DuckDB db(db_path.c_str());
    duckdb::Connection con(db);
    con.Query("LOAD 'anofox_forecast.duckdb_extension'");
    
    // Build product list for IN clause
    std::string product_list;
    for (size_t i = 0; i < product_ids.size(); i++) {
        if (i > 0) product_list += ", ";
        product_list += "'" + product_ids[i] + "'";
    }
    
    // Forecast all products in batch (DuckDB parallelizes)
    std::string query = 
        "CREATE TABLE forecasts_batch AS "
        "SELECT * FROM TS_FORECAST_BY("
        "  (SELECT * FROM sales WHERE product_id IN (" + product_list + ")), "
        "  product_id, date, amount, 'AutoETS', 28, {'seasonal_period': 7}"
        ")";
    
    con.Query(query);
    
    auto count = con.Query("SELECT COUNT(DISTINCT product_id) FROM forecasts_batch");
    std::cout << "Batch processed: " << count->GetValue(0, 0).ToString() 
              << " products" << std::endl;
}

int main() {
    std::vector<std::string> all_products = {"P001", "P002", "P003", /* ... */ "P999"};
    
    // Process in batches
    size_t batch_size = 100;
    for (size_t i = 0; i < all_products.size(); i += batch_size) {
        size_t end = std::min(i + batch_size, all_products.size());
        std::vector<std::string> batch(all_products.begin() + i, 
                                      all_products.begin() + end);
        
        process_batch("warehouse.duckdb", batch);
    }
    
    return 0;
}
```

## Data Loading

### From CSV

```cpp
#include "duckdb.hpp"

int main() {
    duckdb::DuckDB db(nullptr);
    duckdb::Connection con(db);
    con.Query("LOAD 'anofox_forecast.duckdb_extension'");
    
    // Load CSV
    con.Query("CREATE TABLE sales AS SELECT * FROM read_csv('sales.csv')");
    
    // Forecast
    auto forecast = con.Query(R"(
        SELECT * FROM TS_FORECAST_BY('sales', product_id, date, amount,
                                     'AutoETS', 28, {'seasonal_period': 7})
    )");
    
    // Export results
    con.Query("COPY (SELECT * FROM forecast) TO 'forecasts.parquet' (FORMAT PARQUET)");
    
    return 0;
}
```

### From Memory (Vector/Array)

```cpp
#include "duckdb.hpp"
#include <vector>

int main() {
    duckdb::DuckDB db(nullptr);
    duckdb::Connection con(db);
    con.Query("LOAD 'anofox_forecast.duckdb_extension'");
    
    // C++ vectors
    std::vector<std::string> dates = {"2023-01-01", "2023-01-02", /* ... */};
    std::vector<double> amounts = {102.3, 119.5, /* ... */};
    
    // Create table from vectors (using DuckDB's appender)
    con.Query("CREATE TABLE sales (date DATE, amount DOUBLE)");
    
    duckdb::Appender appender(con, "sales");
    for (size_t i = 0; i < dates.size(); i++) {
        appender.BeginRow();
        appender.Append(duckdb::Value::DATE(dates[i]));
        appender.Append(duckdb::Value::DOUBLE(amounts[i]));
        appender.EndRow();
    }
    appender.Close();
    
    // Forecast
    auto result = con.Query(R"(
        SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, 
                                  {'seasonal_period': 7})
    )");
    
    result->Print();
    
    return 0;
}
```

## Error Handling

```cpp
#include "duckdb.hpp"
#include <iostream>
#include <exception>

bool safe_forecast(duckdb::Connection &con, const std::string &product_id) {
    try {
        auto result = con.Query(
            "SELECT * FROM TS_FORECAST("
            "  (SELECT * FROM sales WHERE product_id = '" + product_id + "'), "
            "  date, amount, 'AutoETS', 28, {'seasonal_period': 7}"
            ")"
        );
        
        std::cout << "Forecasted " << result->RowCount() << " points for " 
                  << product_id << std::endl;
        return true;
        
    } catch (const duckdb::Exception &e) {
        std::string error = e.what();
        
        if (error.find("too short") != std::string::npos) {
            std::cerr << product_id << ": Insufficient data" << std::endl;
        } else if (error.find("constant") != std::string::npos) {
            std::cerr << product_id << ": Constant series" << std::endl;
        } else {
            std::cerr << product_id << ": Error - " << error << std::endl;
        }
        
        return false;
    }
}

int main() {
    duckdb::DuckDB db(nullptr);
    duckdb::Connection con(db);
    con.Query("LOAD 'anofox_forecast.duckdb_extension'");
    
    std::vector<std::string> products = {"P001", "P002", "P003"};
    
    for (const auto &pid : products) {
        safe_forecast(con, pid);
    }
    
    return 0;
}
```

## High-Performance Patterns

### Memory-Mapped Database

```cpp
#include "duckdb.hpp"

int main() {
    // Use persistent database for faster repeated access
    duckdb::DuckDB db("warehouse.duckdb");
    duckdb::Connection con(db);
    con.Query("LOAD 'anofox_forecast.duckdb_extension'");
    
    // Tables persist between runs
    // First run: loads data
    // Subsequent runs: instant access
    
    auto forecast = con.Query(R"(
        SELECT * FROM TS_FORECAST_BY('sales', product_id, date, amount,
                                     'AutoETS', 28, {'seasonal_period': 7})
    )");
    
    forecast->Print();
    
    return 0;
}
```

### Parallel Execution

```cpp
#include "duckdb.hpp"
#include <thread>
#include <vector>

void forecast_thread(const std::string &db_path, 
                     const std::vector<std::string> &products,
                     int thread_id) {
    duckdb::DuckDB db(db_path.c_str());
    duckdb::Connection con(db);
    con.Query("LOAD 'anofox_forecast.duckdb_extension'");
    
    // Each thread forecasts a batch
    std::string product_list;
    for (size_t i = 0; i < products.size(); i++) {
        if (i > 0) product_list += ", ";
        product_list += "'" + products[i] + "'";
    }
    
    std::string query = 
        "INSERT INTO forecasts "
        "SELECT * FROM TS_FORECAST_BY("
        "  (SELECT * FROM sales WHERE product_id IN (" + product_list + ")), "
        "  product_id, date, amount, 'AutoETS', 28, {'seasonal_period': 7}"
        ")";
    
    con.Query(query);
    
    std::cout << "Thread " << thread_id << " completed" << std::endl;
}

int main() {
    // Initialize output table
    duckdb::DuckDB db("warehouse.duckdb");
    duckdb::Connection con(db);
    con.Query("LOAD 'anofox_forecast.duckdb_extension'");
    con.Query("CREATE TABLE IF NOT EXISTS forecasts AS SELECT * FROM TS_FORECAST_BY(...) WHERE 1=0");
    
    // Split products across threads
    std::vector<std::vector<std::string>> batches = {
        {"P001", "P002", "P003"},
        {"P004", "P005", "P006"},
        {"P007", "P008", "P009"}
    };
    
    std::vector<std::thread> threads;
    for (size_t i = 0; i < batches.size(); i++) {
        threads.emplace_back(forecast_thread, "warehouse.duckdb", batches[i], i);
    }
    
    for (auto &t : threads) {
        t.join();
    }
    
    std::cout << "All threads completed" << std::endl;
    
    return 0;
}
```

## Integration with C++ Libraries

### With Eigen (Linear Algebra)

```cpp
#include "duckdb.hpp"
#include <Eigen/Dense>
#include <iostream>

Eigen::VectorXd get_forecast_vector(duckdb::Connection &con, 
                                     const std::string &product_id) {
    auto result = con.Query(
        "SELECT point_forecast "
        "FROM TS_FORECAST("
        "  (SELECT * FROM sales WHERE product_id = '" + product_id + "'), "
        "  date, amount, 'AutoETS', 28, {'seasonal_period': 7}"
        ")"
    );
    
    Eigen::VectorXd forecasts(result->RowCount());
    for (size_t i = 0; i < result->RowCount(); i++) {
        forecasts(i) = result->GetValue(0, i).GetValue<double>();
    }
    
    return forecasts;
}

int main() {
    duckdb::DuckDB db(nullptr);
    duckdb::Connection con(db);
    con.Query("LOAD 'anofox_forecast.duckdb_extension'");
    
    // Get forecast as Eigen vector
    Eigen::VectorXd fc = get_forecast_vector(con, "P001");
    
    // Perform linear algebra operations
    std::cout << "Mean: " << fc.mean() << std::endl;
    std::cout << "Std: " << std::sqrt((fc.array() - fc.mean()).square().sum() / fc.size()) << std::endl;
    
    return 0;
}
```

### With Boost

```cpp
#include "duckdb.hpp"
#include <boost/date_time/gregorian/gregorian.hpp>
#include <iostream>

int main() {
    using namespace boost::gregorian;
    
    duckdb::DuckDB db(nullptr);
    duckdb::Connection con(db);
    con.Query("LOAD 'anofox_forecast.duckdb_extension'");
    
    // Get forecast
    auto result = con.Query(R"(
        SELECT date_col::VARCHAR, point_forecast 
        FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, 
                         {'seasonal_period': 7})
    )");
    
    // Convert dates to boost::date
    for (size_t row = 0; row < result->RowCount(); row++) {
        std::string date_str = result->GetValue(0, row).GetValue<std::string>();
        double forecast = result->GetValue(1, row).GetValue<double>();
        
        date d = from_string(date_str);
        std::cout << d << ": " << forecast << std::endl;
    }
    
    return 0;
}
```

## Persistent Connection Pattern

```cpp
#include "duckdb.hpp"
#include <memory>

class ForecastEngine {
private:
    std::shared_ptr<duckdb::DuckDB> db_;
    std::shared_ptr<duckdb::Connection> con_;
    bool initialized_;
    
public:
    ForecastEngine(const std::string &db_path = ":memory:") 
        : initialized_(false) {
        db_ = std::make_shared<duckdb::DuckDB>(db_path.c_str());
        con_ = std::make_shared<duckdb::Connection>(*db_);
    }
    
    void initialize() {
        if (!initialized_) {
            con_->Query("LOAD 'anofox_forecast.duckdb_extension'");
            initialized_ = true;
        }
    }
    
    std::unique_ptr<duckdb::QueryResult> forecast(
        const std::string &table, 
        const std::string &group_col,
        const std::string &date_col,
        const std::string &value_col,
        int horizon,
        const std::string &model = "AutoETS") {
        
        initialize();
        
        std::string query = 
            "SELECT * FROM TS_FORECAST_BY('" + table + "', " + group_col + ", " + 
            date_col + ", " + value_col + ", '" + model + "', " + 
            std::to_string(horizon) + ", {'seasonal_period': 7})";
        
        return con_->Query(query);
    }
    
    std::unique_ptr<duckdb::QueryResult> query(const std::string &sql) {
        initialize();
        return con_->Query(sql);
    }
};

int main() {
    ForecastEngine engine("warehouse.duckdb");
    
    // Multiple operations on same connection
    auto stats = engine.query("SELECT * FROM TS_STATS('sales', product_id, date, amount)");
    auto forecast = engine.forecast("sales", "product_id", "date", "amount", 28);
    auto metrics = engine.query("SELECT TS_MAE(...) FROM ...");
    
    stats->Print();
    forecast->Print();
    
    return 0;
}
```

## Best Practices

### 1. RAII for Connection Management

```cpp
class DuckDBGuard {
private:
    duckdb::DuckDB db_;
    duckdb::Connection con_;
    
public:
    DuckDBGuard(const std::string &path = ":memory:") 
        : db_(path.c_str()), con_(db_) {
        con_.Query("LOAD 'anofox_forecast.duckdb_extension'");
    }
    
    ~DuckDBGuard() = default;  // RAII cleanup
    
    duckdb::Connection& connection() { return con_; }
};

int main() {
    {
        DuckDBGuard guard;
        auto result = guard.connection().Query("SELECT * FROM TS_FORECAST(...)");
        result->Print();
    }  // Automatic cleanup
    
    return 0;
}
```

### 2. Logging

```cpp
#include "duckdb.hpp"
#include <spdlog/spdlog.h>

int main() {
    spdlog::info("Starting forecast application");
    
    duckdb::DuckDB db(nullptr);
    duckdb::Connection con(db);
    con.Query("LOAD 'anofox_forecast.duckdb_extension'");
    
    spdlog::info("Extension loaded");
    
    auto result = con.Query(R"(
        SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, 
                                  {'seasonal_period': 7})
    )");
    
    spdlog::info("Generated {} forecast points", result->RowCount());
    
    return 0;
}
```

### 3. Configuration

```cpp
#include "duckdb.hpp"
#include <nlohmann/json.hpp>
#include <fstream>

struct Config {
    std::string db_path;
    std::string extension_path;
    std::string model;
    int horizon;
    int seasonal_period;
};

Config load_config(const std::string &path) {
    std::ifstream file(path);
    nlohmann::json j;
    file >> j;
    
    return Config{
        j["db_path"],
        j["extension_path"],
        j.value("model", "AutoETS"),
        j.value("horizon", 28),
        j.value("seasonal_period", 7)
    };
}

int main() {
    auto config = load_config("config.json");
    
    duckdb::DuckDB db(config.db_path.c_str());
    duckdb::Connection con(db);
    con.Query("LOAD '" + config.extension_path + "'");
    
    // Use config
    std::string query = 
        "SELECT * FROM TS_FORECAST('sales', date, amount, '" + config.model + "', " +
        std::to_string(config.horizon) + ", {'seasonal_period': " + 
        std::to_string(config.seasonal_period) + "})";
    
    auto result = con.Query(query);
    result->Print();
    
    return 0;
}
```

## Summary

**Why Use from C++?**
- ✅ Maximum performance (zero-copy where possible)
- ✅ Embedding in existing C++ applications
- ✅ Low-latency forecasting services
- ✅ Full control over execution
- ✅ Type-safe interface

**Typical C++ Workflow**:
```
Load data → Create table → Query SQL → Process results → Output
```

**Performance**:
- Lowest latency (embedded, no IPC)
- Direct memory access
- Compiled optimizations
- Perfect for:
  - Real-time forecasting services
  - High-frequency trading
  - Low-latency APIs
  - Embedded systems

**Use Cases**:
- Microservices (forecast API)
- Real-time analytics
- Embedded forecasting in trading systems
- High-performance batch processing
- Integration with C++ numerical libraries

---

**Next**: [Rust Usage Guide](85_rust_integration.md) | [Python Usage Guide](81_python_integration.md)

**C++ + DuckDB**: Ultimate performance for production forecasting! ⚡

