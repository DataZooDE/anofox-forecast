cmake_minimum_required(VERSION 3.5)

# Set extension name here
set(TARGET_NAME anofox_forecast)

# Force C++17 standard (required by anofox-time)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# DuckDB's extension distribution supports vcpkg
find_package(OpenSSL REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(Threads REQUIRED)

# Include anofox-time headers
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/anofox-time/include)
include_directories(${EIGEN3_INCLUDE_DIR})

set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})
include_directories(src/include)

# Include only the anofox-time source files we need (not all of them)
set(ANOFOX_TIME_SOURCES
    anofox-time/src/models/sma.cpp
    anofox-time/src/models/naive.cpp
    anofox-time/src/models/seasonal_naive.cpp
    anofox-time/src/models/ses.cpp
    anofox-time/src/models/theta.cpp
    anofox-time/src/models/holt.cpp
    anofox-time/src/models/holt_winters.cpp
    anofox-time/src/models/ets.cpp
    anofox-time/src/models/auto_ets.cpp
    anofox-time/src/models/mfles.cpp
    anofox-time/src/models/auto_mfles.cpp
    anofox-time/src/models/mstl_forecaster.cpp
    anofox-time/src/models/auto_mstl.cpp
    anofox-time/src/models/arima.cpp
    anofox-time/src/models/auto_arima.cpp
    anofox-time/src/seasonality/stl.cpp
    anofox-time/src/seasonality/mstl.cpp
    anofox-time/src/utils/logging.cpp
    anofox-time/src/utils/metrics.cpp
    anofox-time/src/utils/nelder_mead.cpp
)

# Update extension sources
set(EXTENSION_SOURCES 
    src/anofox_forecast_extension.cpp
    src/forecast_table_function.cpp
    src/forecast_aggregate.cpp
    src/model_factory.cpp
    src/time_series_builder.cpp
    src/anofox_time_wrapper.cpp
    ${ANOFOX_TIME_SOURCES}
)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

# Add compile definition to disable logging
target_compile_definitions(${EXTENSION_NAME} PRIVATE ANOFOX_NO_LOGGING)
target_compile_definitions(${LOADABLE_EXTENSION_NAME} PRIVATE ANOFOX_NO_LOGGING)

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