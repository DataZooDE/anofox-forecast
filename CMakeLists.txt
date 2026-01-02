cmake_minimum_required(VERSION 3.20)

# Set extension name
set(TARGET_NAME anofox_forecast)
project(${TARGET_NAME})

# DuckDB extension boilerplate
include_directories(src/include)

# Fetch Corrosion for Rust integration
include(FetchContent)
FetchContent_Declare(
    Corrosion
    GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
    GIT_TAG v0.5.2
)
FetchContent_MakeAvailable(Corrosion)

# Import Rust FFI crate
# Note: Corrosion converts hyphens to underscores in target names
corrosion_import_crate(
    MANIFEST_PATH ${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml
    CRATES anofox-fcst-ffi
)

# The Rust target name (with underscores)
set(RUST_LIB anofox_fcst_ffi)

# Set Rust build options for release
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    corrosion_set_env_vars(${RUST_LIB} "CARGO_PROFILE_RELEASE_LTO=true")
endif()

# Extension sources
set(EXTENSION_SOURCES
    src/anofox_forecast_extension.cpp
    src/table_functions/ts_stats.cpp
    src/table_functions/ts_data_quality.cpp
    src/table_functions/ts_forecast.cpp
    src/table_functions/ts_seasonality.cpp
    src/table_functions/ts_decomposition.cpp
    src/table_functions/ts_changepoints.cpp
    src/table_functions/ts_features.cpp
    src/table_functions/ts_imputation.cpp
    src/table_functions/ts_filter.cpp
    src/table_functions/ts_gaps.cpp
    src/table_functions/ts_fill_forward_operator.cpp
    src/scalar_functions/metrics.cpp
    src/aggregate_functions/ts_forecast_agg.cpp
    src/aggregate_functions/ts_changepoints_agg.cpp
    src/aggregate_functions/ts_features_agg.cpp
    src/macros/ts_macros.cpp
)

# Build the static extension
build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})

# Build the loadable extension
build_loadable_extension(${TARGET_NAME} "" ${EXTENSION_SOURCES})

# Link Rust library and platform-specific libs
if(UNIX AND NOT APPLE)
    target_link_libraries(${TARGET_NAME}_extension ${RUST_LIB} pthread dl m)
    target_link_libraries(${TARGET_NAME}_loadable_extension ${RUST_LIB} pthread dl m)
elseif(WIN32)
    target_link_libraries(${TARGET_NAME}_extension ${RUST_LIB} bcrypt)
    target_link_libraries(${TARGET_NAME}_loadable_extension ${RUST_LIB} bcrypt)
else()
    target_link_libraries(${TARGET_NAME}_extension ${RUST_LIB})
    target_link_libraries(${TARGET_NAME}_loadable_extension ${RUST_LIB})
endif()

# Add Rust library to DuckDB export set
install(TARGETS ${RUST_LIB}
    EXPORT "${DUCKDB_EXPORT_SET}"
    LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
    ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")

# Install extension
install(TARGETS ${TARGET_NAME}_extension
    EXPORT "${DUCKDB_EXPORT_SET}"
    LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
    ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
