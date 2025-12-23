cmake_minimum_required(VERSION 3.5)

# Set extension name here
set(TARGET_NAME anofox_forecast)

project(${TARGET_NAME} LANGUAGES C CXX)

# Force C++17 standard (required by anofox-time)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Logging Control ---
option(ENABLE_LOGGING "Enable debug/info logging output (disable for production)" OFF)

if(ENABLE_LOGGING)
    message(STATUS "anofox-forecast: Logging ENABLED (development mode)")
else()
    message(STATUS "anofox-forecast: Logging DISABLED (production mode)")
endif()

# DuckDB's extension distribution supports vcpkg
find_package(Threads REQUIRED)

# --- Telemetry ---
# Build telemetry as OBJECT library to avoid link order issues with DuckDB symbols
# The telemetry library uses DuckDB's StringUtil::Format which depends on exception symbols
find_package(OpenSSL REQUIRED)

add_library(posthog_telemetry_obj OBJECT
    ${CMAKE_CURRENT_SOURCE_DIR}/posthog-telemetry/src/telemetry.cpp
)

target_include_directories(posthog_telemetry_obj PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/posthog-telemetry/include
    ${CMAKE_CURRENT_SOURCE_DIR}/duckdb/src/include
    ${CMAKE_CURRENT_SOURCE_DIR}/duckdb/third_party
    ${CMAKE_CURRENT_SOURCE_DIR}/duckdb/third_party/httplib
    ${OPENSSL_INCLUDE_DIR}
)

target_compile_features(posthog_telemetry_obj PUBLIC cxx_std_17)

# Find Eigen3 - prefer vcpkg's version when available
# When using vcpkg, use CONFIG mode for proper integration
if(VCPKG_BUILD OR DEFINED VCPKG_TARGET_TRIPLET)
    # Using vcpkg - Eigen3 should be available via vcpkg
    find_package(Eigen3 CONFIG REQUIRED)
elseif(CLANG_TIDY)
    # clang-tidy doesn't use vcpkg, make Eigen3 optional
    find_package(Eigen3 QUIET)
else()
    # Try CONFIG mode first (for vcpkg), fall back to MODULE mode (system install)
    find_package(Eigen3 CONFIG QUIET)
    if(NOT Eigen3_FOUND)
        find_package(Eigen3 REQUIRED)
    endif()
endif()

# Include anofox-time headers
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/anofox-time/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/anofox-time/third_party/LBFGSpp/include)
# Note: When using Eigen3::Eigen target, include_directories is handled automatically

set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)
include_directories(src/include)

# Include only the anofox-time source files we need (not all of them)
set(ANOFOX_TIME_SOURCES
    # Basic models
    anofox-time/src/models/sma.cpp
    anofox-time/src/models/naive.cpp
    anofox-time/src/models/seasonal_naive.cpp
    anofox-time/src/models/ses.cpp
    anofox-time/src/models/ses_optimized.cpp
    anofox-time/src/models/random_walk_drift.cpp
    # Holt models
    anofox-time/src/models/holt.cpp
    anofox-time/src/models/holt_winters.cpp
    # Theta variants
    anofox-time/src/models/theta_pegels.cpp
    anofox-time/src/models/theta.cpp
    anofox-time/src/models/theta_utils.cpp
    anofox-time/src/models/optimized_theta.cpp
    anofox-time/src/models/dynamic_theta.cpp
    anofox-time/src/models/dynamic_optimized_theta.cpp
    anofox-time/src/models/auto_theta.cpp
    # Seasonal models
    anofox-time/src/models/seasonal_es.cpp
    anofox-time/src/models/seasonal_es_optimized.cpp
    anofox-time/src/models/seasonal_window_average.cpp
    # State space
    anofox-time/src/models/ets.cpp
    anofox-time/src/models/ets_core_statsforecast.cpp
    anofox-time/src/models/auto_ets.cpp
    # Multiple seasonality
    anofox-time/src/models/mfles.cpp
    anofox-time/src/models/auto_mfles.cpp
    anofox-time/src/models/method_name_wrapper.cpp
    anofox-time/src/models/mstl_forecaster.cpp
    anofox-time/src/models/auto_mstl.cpp
    anofox-time/src/models/tbats.cpp
    anofox-time/src/models/auto_tbats.cpp
    # Intermittent demand
    anofox-time/src/models/croston_classic.cpp
    anofox-time/src/models/croston_optimized.cpp
    anofox-time/src/models/croston_sba.cpp
    anofox-time/src/models/adida.cpp
    anofox-time/src/models/imapa.cpp
    anofox-time/src/models/tsb.cpp
    # Seasonality decomposition
    anofox-time/src/seasonality/stl.cpp
    anofox-time/src/seasonality/mstl.cpp
    anofox-time/src/seasonality/detector.cpp
    anofox-time/src/seasonality/analyzer.cpp
    # Changepoint detection
    anofox-time/src/changepoint/bocpd.cpp
    # Utilities
    anofox-time/src/utils/logging.cpp
    anofox-time/src/utils/metrics.cpp
    anofox-time/src/utils/nelder_mead.cpp
    anofox-time/src/utils/intermittent_utils.cpp
    anofox-time/src/utils/robust_regression.cpp
    anofox-time/src/utils/cross_validation.cpp
    anofox-time/src/features/feature_math.cpp
    anofox-time/src/features/feature_registry.cpp
    anofox-time/src/features/feature_calculators.cpp
    # Optimization
    anofox-time/src/optimization/lbfgs_optimizer.cpp
    anofox-time/src/optimization/ets_gradients.cpp
    anofox-time/src/optimization/ets_gradients_simd.cpp
    anofox-time/src/optimization/ets_gradients_checkpointing.cpp
    anofox-time/src/optimization/theta_gradients.cpp
)

# Add ARIMA models if Eigen3 is available
if(Eigen3_FOUND)
    list(APPEND ANOFOX_TIME_SOURCES
        anofox-time/src/models/arima.cpp
        anofox-time/src/models/auto_arima.cpp
    )
endif()

# Update extension sources
set(EXTENSION_SOURCES 
    src/anofox_forecast_extension.cpp
    src/forecast_table_function.cpp
    src/forecast_aggregate.cpp
    src/ts_features_function.cpp
    src/metrics_function.cpp
    src/seasonality_function.cpp
    src/changepoint_function.cpp
    src/eda_macros.cpp
    src/eda_bind_replace.cpp
    # Order matters for static library linking: consumer before producer
    # data_prep_macros.cpp USES CreateTSFillGapsOperatorTableFunction()
    src/data_prep_macros.cpp
    # ts_fill_gaps_function.cpp DEFINES CreateTSFillGapsOperatorTableFunction()
    src/ts_fill_gaps_function.cpp
    # ts_fill_forward_function.cpp DEFINES CreateTSFillForwardOperatorTableFunction()
    src/ts_fill_forward_function.cpp
    src/mstl_decomposition_function.cpp
    src/ts_forecast_by_test_function.cpp
    src/data_prep_bind_replace.cpp
    src/model_factory.cpp
    src/time_series_builder.cpp
    src/anofox_time_wrapper.cpp
    ${ANOFOX_TIME_SOURCES}
)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

# Add telemetry objects to both extension targets
target_sources(${EXTENSION_NAME} PRIVATE $<TARGET_OBJECTS:posthog_telemetry_obj>)
target_sources(${LOADABLE_EXTENSION_NAME} PRIVATE $<TARGET_OBJECTS:posthog_telemetry_obj>)

# Add telemetry include path for telemetry.hpp header
target_include_directories(${EXTENSION_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/posthog-telemetry/include)
target_include_directories(${LOADABLE_EXTENSION_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/posthog-telemetry/include)

# Enable AVX2 for SIMD optimizations (if supported)
# DISABLED: Causing compatibility issues with older GCC/Eigen3
# include(CheckCXXCompilerFlag)
# check_cxx_compiler_flag("-mavx2" COMPILER_SUPPORTS_AVX2)
# if(COMPILER_SUPPORTS_AVX2)
#     target_compile_options(${EXTENSION_NAME} PRIVATE -mavx2 -mfma)
#     target_compile_options(${LOADABLE_EXTENSION_NAME} PRIVATE -mavx2 -mfma)
#     message(STATUS "AVX2 support enabled for anofox-forecast extension")
# endif()
message(STATUS "AVX2 support DISABLED for compatibility")

# Control logging based on ENABLE_LOGGING option
if(NOT ENABLE_LOGGING)
    target_compile_definitions(${EXTENSION_NAME} PRIVATE ANOFOX_NO_LOGGING)
    target_compile_definitions(${LOADABLE_EXTENSION_NAME} PRIVATE ANOFOX_NO_LOGGING)
endif()

# Link required libraries (no pre-built anofox-time needed)
target_link_libraries(${EXTENSION_NAME}
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_link_libraries(${LOADABLE_EXTENSION_NAME}
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)

if(WIN32)
    target_compile_definitions(${EXTENSION_NAME} PRIVATE _USE_MATH_DEFINES)
    target_compile_definitions(${LOADABLE_EXTENSION_NAME} PRIVATE _USE_MATH_DEFINES)
    # iphlpapi is required for GetAdaptersInfo (used by posthog-telemetry for MAC address)
    target_link_libraries(${EXTENSION_NAME} iphlpapi)
    target_link_libraries(${LOADABLE_EXTENSION_NAME} iphlpapi)
endif()

# Link Eigen3 if available and add compile definition
if(Eigen3_FOUND)
    target_link_libraries(${EXTENSION_NAME} Eigen3::Eigen)
    target_link_libraries(${LOADABLE_EXTENSION_NAME} Eigen3::Eigen)
    target_compile_definitions(${EXTENSION_NAME} PRIVATE HAVE_EIGEN3)
    target_compile_definitions(${LOADABLE_EXTENSION_NAME} PRIVATE HAVE_EIGEN3)
    message(STATUS "Eigen3 found - ARIMA models enabled")
else()
    message(STATUS "Eigen3 not found - ARIMA models disabled (ok for tidy-check)")
endif()

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")

# --- Unit Tests for ts_fill_gaps (must pass before DuckDB binding) ---
option(BUILD_TS_FILL_GAPS_TESTS "Build unit tests for ts_fill_gaps" ON)

# Skip tests for WASM builds - they require DuckDB static library which isn't available
if(BUILD_TS_FILL_GAPS_TESTS AND NOT WASM_LOADABLE_EXTENSIONS)
    enable_testing()
    include(CTest)
    
    # Find Catch2 (DuckDB's version)
    set(CATCH_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/duckdb/third_party/catch")
    
    if(EXISTS "${CATCH_INCLUDE_DIR}/catch.hpp")
        # Create test executable
        add_executable(ts_fill_gaps_tests
            test/cpp/test_ts_fill_gaps.cpp
            src/ts_fill_gaps_function.cpp
        )
        
        # Include directories
        target_include_directories(ts_fill_gaps_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/include
            ${CMAKE_CURRENT_SOURCE_DIR}/duckdb/src/include
            ${CATCH_INCLUDE_DIR}
        )
        
        # Link DuckDB (we need the types and exception classes)
        # Note: This requires DuckDB to be built first
        # For now, we'll link against the extension which includes DuckDB
        target_link_libraries(ts_fill_gaps_tests PRIVATE
            ${EXTENSION_NAME}
            Threads::Threads
        )
        
        # Add test
        add_test(NAME ts_fill_gaps_unit_tests COMMAND ts_fill_gaps_tests)
        
        message(STATUS "ts_fill_gaps unit tests enabled")

        # Create MSTL test executable
        add_executable(mstl_decomposition_tests
            test/cpp/test_mstl_decomposition.cpp
            src/mstl_decomposition_function.cpp
        )
        
        target_include_directories(mstl_decomposition_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/include
            ${CMAKE_CURRENT_SOURCE_DIR}/duckdb/src/include
            ${CATCH_INCLUDE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/anofox-time/include
        )
        
        target_link_libraries(mstl_decomposition_tests PRIVATE
            ${EXTENSION_NAME}
            Threads::Threads
        )
        
        target_compile_definitions(mstl_decomposition_tests PRIVATE DUCKDB_EXTENSION_BUILD)
        
        add_test(NAME mstl_decomposition_unit_tests COMMAND mstl_decomposition_tests)
        message(STATUS "mstl_decomposition unit tests enabled")
    else()
        message(WARNING "Catch2 not found at ${CATCH_INCLUDE_DIR} - skipping ts_fill_gaps unit tests")
    endif()
elseif(WASM_LOADABLE_EXTENSIONS)
    message(STATUS "ts_fill_gaps unit tests disabled for WASM builds")
endif()