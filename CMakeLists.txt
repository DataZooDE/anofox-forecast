cmake_minimum_required(VERSION 3.20)

set(CORROSION_VERBOSE_OUTPUT ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Get installed Rust targets
execute_process(
    COMMAND rustup target list --installed
    OUTPUT_VARIABLE RUST_TARGETS
    ERROR_QUIET
)

# Propagate arch to rust build for CI
set(Rust_CARGO_TARGET "")
if("${OS_NAME}" STREQUAL "linux")
    if ("${OS_ARCH}" STREQUAL "arm64")
        set(Rust_CARGO_TARGET "aarch64-unknown-linux-gnu")
    elseif("${CMAKE_CXX_COMPILER}" MATCHES "aarch64")
        set(Rust_CARGO_TARGET "aarch64-unknown-linux-gnu")
    else()
        string(FIND "${RUST_TARGETS}" "musl" MUSL_TARGET_FOUND)
        if(NOT MUSL_TARGET_FOUND EQUAL -1)
            set(Rust_CARGO_TARGET "x86_64-unknown-linux-musl")
        else()
            set(Rust_CARGO_TARGET "x86_64-unknown-linux-gnu")
        endif()
    endif()
elseif("${OS_NAME}" STREQUAL "osx")
    if ("${OSX_BUILD_ARCH}" STREQUAL "arm64")
        set(Rust_CARGO_TARGET "aarch64-apple-darwin")
    elseif ("${OSX_BUILD_ARCH}" STREQUAL "x86_64")
        set(Rust_CARGO_TARGET "x86_64-apple-darwin")
    elseif ("${OS_ARCH}" STREQUAL "arm64")
        set(Rust_CARGO_TARGET "aarch64-apple-darwin")
    endif()
elseif(WIN32)
    if (MINGW AND "${OS_ARCH}" STREQUAL "arm64")
        set(Rust_CARGO_TARGET "aarch64-pc-windows-gnu")
    elseif (MINGW AND "${OS_ARCH}" STREQUAL "amd64")
        set(Rust_CARGO_TARGET "x86_64-pc-windows-gnu")
    elseif (MSVC AND "${OS_ARCH}" STREQUAL "arm64")
        set(Rust_CARGO_TARGET "aarch64-pc-windows-msvc")
    elseif (MSVC AND "${OS_ARCH}" STREQUAL "amd64")
        set(Rust_CARGO_TARGET "x86_64-pc-windows-msvc")
    endif()
endif()

# Check for WASM target
string(FIND "${RUST_TARGETS}" "wasm32-unknown-emscripten" WASM_TARGET_FOUND)
if (NOT WASM_TARGET_FOUND EQUAL -1)
    set(Rust_CARGO_TARGET "wasm32-unknown-emscripten")
endif()

message(STATUS "Rust_CARGO_TARGET: ${Rust_CARGO_TARGET}")
message(STATUS "OS_NAME: ${OS_NAME}")
message(STATUS "OS_ARCH: ${OS_ARCH}")

# Detect WASM build for conditional compilation
if(DEFINED ENV{DUCKDB_WASM_PLATFORM} OR "${DUCKDB_EXPLICIT_PLATFORM}" MATCHES "^wasm" OR "${Rust_CARGO_TARGET}" STREQUAL "wasm32-unknown-emscripten")
    set(WASM_BUILD TRUE)
    message(STATUS "Building for WASM platform")
else()
    set(WASM_BUILD FALSE)
endif()

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
if(WASM_BUILD)
    # For WASM, disable telemetry feature (networking not available)
    corrosion_import_crate(
        MANIFEST_PATH ${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml
        CRATES anofox-fcst-ffi
        FEATURES ""
        NO_DEFAULT_FEATURES
    )
else()
    corrosion_import_crate(
        MANIFEST_PATH ${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml
        CRATES anofox-fcst-ffi
    )
endif()

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
    src/table_functions/ts_fill_gaps_native.cpp
    src/table_functions/ts_fill_forward_native.cpp
    src/table_functions/ts_fill_forward_operator.cpp
    src/table_functions/ts_periods.cpp
    src/table_functions/ts_peaks.cpp
    src/table_functions/ts_detrend.cpp
    src/scalar_functions/metrics.cpp
    src/scalar_functions/conformal.cpp
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
if(WASM_BUILD)
    # WASM: minimal linking, no system libraries
    target_link_libraries(${TARGET_NAME}_extension ${RUST_LIB})
    target_link_libraries(${TARGET_NAME}_loadable_extension ${RUST_LIB})
elseif(UNIX AND NOT APPLE)
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
