# Anofox Forecast - DuckDB Time Series Extension

A DuckDB extension that brings powerful time series forecasting capabilities using the [anofox-time](https://github.com/anofox/anofox-time) library.

## Features (Phase 1)

✅ **Three baseline forecasting methods:**
- **Naive**: Random walk forecasting (last value carried forward)
- **SMA**: Simple Moving Average forecasting
- **SeasonalNaive**: Seasonal naive forecasting (planned)

✅ **SQL-native interface** using table-in-out functions  
✅ **Comprehensive error handling** with detailed validation  
✅ **Debug logging** for easy troubleshooting  
✅ **Fast C++ implementation** leveraging anofox-time library  

## Quick Start

### Build

```bash
# Build the extension
make debug

# Or for production:
make release
```

### Usage

```sql
-- Load the extension
LOAD 'build/debug/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- Create sample data
CREATE TABLE sales (date TIMESTAMP, amount DOUBLE);
INSERT INTO sales VALUES 
    ('2024-01-01', 100), ('2024-01-02', 105), ('2024-01-03', 110),
    ('2024-01-04', 115), ('2024-01-05', 120);

-- Generate forecasts
SELECT 
    forecast_step,
    point_forecast,
    model_name
FROM FORECAST('date', 'amount', 'Naive', 7, NULL)
ORDER BY forecast_step;
```

## Architecture

### Components

```
src/
├── anofox_forecast_extension.cpp    # Extension entry point
├── forecast_table_function.cpp      # FORECAST table function implementation
├── model_factory.cpp                # Factory for creating forecast models
├── time_series_builder.cpp          # DuckDB ↔ anofox-time data conversion
└── anofox_time_wrapper.cpp          # Wrapper to isolate anofox-time from DuckDB
```

### Design Highlights

- **Table-in-out functions**: Follows DuckDB's table-in-out pattern for optimal performance
- **Namespace isolation**: Careful separation to avoid library conflicts (fmt, spdlog)
- **Type safety**: Strong typing with comprehensive validation
- **Memory management**: Proper ownership and lifecycle management

## Implementation Details

### Library Integration

The extension integrates the anofox-time library by:
1. Compiling only required source files directly into the extension
2. Using `ANOFOX_NO_LOGGING` to disable spdlog dependency
3. Wrapping all anofox-time types to prevent namespace pollution

### Build System

- **CMake-based**: Integrates with DuckDB's extension build system
- **Dependencies**: Eigen3, OpenSSL, Threads (spdlog disabled)
- **C++17 required**: For anofox-time's modern C++ features

## Testing

```bash
# Run all tests
make test_debug

# Run specific test
./build/debug/test/unittest "test/sql/forecast_simple.test"
```

### Test Coverage

- ✅ Extension loading
- ✅ Naive model forecasting
- ✅ SMA model forecasting
- ✅ Multiple forecast horizons
- ✅ Column validation
- ✅ Error handling

## Roadmap

### Phase 1 (Current) ✅
- [x] Basic extension structure
- [x] FORECAST table function
- [x] SMA, Naive models
- [x] Comprehensive error handling
- [x] Test suite
- [x] Documentation

### Phase 2 (Planned)
- [ ] GROUP BY support for batch forecasting
- [ ] All 30+ models from anofox-time
- [ ] STRUCT parameter support with named fields
- [ ] Proper statistical prediction intervals
- [ ] ENSEMBLE() table function
- [ ] BACKTEST() table function
- [ ] Scalar utility functions

## Technical Notes

### Namespace Conflict Resolution

DuckDB includes its own version of the fmt library, which conflicts with spdlog's bundled fmt. We resolved this by:
1. Building anofox-time without spdlog (`ANOFOX_NO_LOGGING`)
2. Modifying anofox-time's logging.hpp to make spdlog optional
3. Compiling only needed source files into the extension

### Symbol Visibility

The anofox-time library is compiled with hidden symbol visibility to prevent conflicts with DuckDB's internal libraries.

## Development

### Adding New Models

To add a new forecast model:

1. Add the model header include to `anofox_time_wrapper.hpp`
2. Create factory method in `AnofoxTimeWrapper`
3. Add model name to `ModelFactory::GetSupportedModels()`
4. Implement parameter validation in `ModelFactory::ValidateModelParams()`
5. Add create case in `ModelFactory::Create()`
6. Update tests and documentation

### Debug Logging

All major operations include debug output:
```cpp
std::cout << "[DEBUG] Operation description" << std::endl;
```

To see debug output, run tests with stderr:
```bash
./build/debug/test/unittest "test/sql/yourtest.test" 2>&1
```

## License

See LICENSE file.

## Credits

Built on top of the excellent [anofox-time](https://github.com/anofox/anofox-time) forecasting library.
