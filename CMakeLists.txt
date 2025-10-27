cmake_minimum_required(VERSION 3.5)

# Set extension name here
set(TARGET_NAME anofox_forecast)

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
find_package(OpenSSL REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(Threads REQUIRED)

# Include anofox-time headers
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/anofox-time/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/anofox-time/third_party/LBFGSpp/include)
include_directories(${EIGEN3_INCLUDE_DIR})

set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})
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
    anofox-time/src/models/optimized_theta.cpp
    anofox-time/src/models/dynamic_theta.cpp
    anofox-time/src/models/dynamic_optimized_theta.cpp
    # Seasonal models
    anofox-time/src/models/seasonal_es.cpp
    anofox-time/src/models/seasonal_es_optimized.cpp
    anofox-time/src/models/seasonal_window_average.cpp
    # ARIMA
    anofox-time/src/models/arima.cpp
    anofox-time/src/models/auto_arima.cpp
    # State space
    anofox-time/src/models/ets.cpp
    anofox-time/src/models/ets_core_statsforecast.cpp
    anofox-time/src/models/auto_ets.cpp
    # Multiple seasonality
    anofox-time/src/models/mfles.cpp
    anofox-time/src/models/auto_mfles.cpp
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
    # Utilities
    anofox-time/src/utils/logging.cpp
    anofox-time/src/utils/metrics.cpp
    anofox-time/src/utils/nelder_mead.cpp
    anofox-time/src/utils/intermittent_utils.cpp
    # Optimization
    anofox-time/src/optimization/lbfgs_optimizer.cpp
    anofox-time/src/optimization/ets_gradients.cpp
    anofox-time/src/optimization/ets_gradients_simd.cpp
    anofox-time/src/optimization/ets_gradients_checkpointing.cpp
)

# Update extension sources
set(EXTENSION_SOURCES 
    src/anofox_forecast_extension.cpp
    src/forecast_table_function.cpp
    src/forecast_aggregate.cpp
    src/metrics_function.cpp
    src/model_factory.cpp
    src/time_series_builder.cpp
    src/anofox_time_wrapper.cpp
    ${ANOFOX_TIME_SOURCES}
)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

# Enable AVX2 for SIMD optimizations (if supported)
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-mavx2" COMPILER_SUPPORTS_AVX2)
if(COMPILER_SUPPORTS_AVX2)
    target_compile_options(${EXTENSION_NAME} PRIVATE -mavx2 -mfma)
    target_compile_options(${LOADABLE_EXTENSION_NAME} PRIVATE -mavx2 -mfma)
    message(STATUS "AVX2 support enabled for anofox-forecast extension")
endif()

# Control logging based on ENABLE_LOGGING option
if(NOT ENABLE_LOGGING)
    target_compile_definitions(${EXTENSION_NAME} PRIVATE ANOFOX_NO_LOGGING)
    target_compile_definitions(${LOADABLE_EXTENSION_NAME} PRIVATE ANOFOX_NO_LOGGING)
endif()

# Link required libraries (no pre-built anofox-time needed)
target_link_libraries(${EXTENSION_NAME} 
    Eigen3::Eigen
    Threads::Threads
    OpenSSL::SSL 
    OpenSSL::Crypto
)

target_link_libraries(${LOADABLE_EXTENSION_NAME} 
    Eigen3::Eigen
    Threads::Threads
    OpenSSL::SSL 
    OpenSSL::Crypto
)

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")