# Anofox Forecast - DuckDB Time Series Extension

A DuckDB extension that brings powerful time series forecasting capabilities using the [anofox-time](https://github.com/anofox/anofox-time) library.

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
- [x] Bla

## Technical Notes


### Symbol Visibility

The anofox-time library is compiled with hidden symbol visibility to prevent conflicts with DuckDB's internal libraries.

## License

See LICENSE file.

## Credits

Built on top of the excellent [anofox-time](https://github.com/anofox/anofox-time) forecasting library.
