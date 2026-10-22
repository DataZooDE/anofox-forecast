cmake_minimum_required(VERSION 3.20)

set(CORROSION_VERBOSE_OUTPUT ON)
# Force C++17 globally (CACHE FORCE) so DuckDB's submodule build picks it up too.
# Without this, DuckDB's CMake sets CMAKE_CXX_STANDARD 11 and the static constexpr
# class-type member duckdb::BufferedFileWriter::DEFAULT_OPEN_FLAGS gets emitted as
# a strong symbol in multiple translation units, breaking the plan_serializer link
# on arm64 / GCC 14 (duckdb/duckdb#22097). In C++17 the member is implicitly inline.
set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ standard to enforce" FORCE)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Allow the GNU linker to pick the first of duplicate, byte-identical symbols.
# Added for DuckDB's former v1.4 LTS line (no longer supported), which emitted the
# constexpr static members duckdb::LogicalType::FLOAT,
# ::DOUBLE, ::VARCHAR, duckdb::MultiFileReader::COLUMN_IDENTIFIER_* etc. as strong symbols
# in more than one static archive (libduckdb_static.a and libcore_functions_extension.a /
# libparquet_extension.a). Combined with the forced C++17 above, strict GNU ld on
# glibc/GCC-14 rejects these as "multiple definition" when linking libduckdb.so /
# libsqlite3_api_wrapper.so (the musl toolchain's linker already tolerates them). The
# definitions are identical, so picking the first is safe; this is a no-op on the v1.5
# line, which emits no duplicates. Kept until the 2.0 line is confirmed not to need it.
# GNU-ld only -> scope to Linux (excludes Darwin/Windows/WASM).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    foreach(_lf CMAKE_EXE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS CMAKE_MODULE_LINKER_FLAGS)
        set(${_lf} "${${_lf}} -Wl,--allow-multiple-definition" CACHE STRING "" FORCE)
    endforeach()
endif()

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

# WASM target. Keyed on EMSCRIPTEN alone, not on the *default* rustup toolchain's
# installed targets: WASM builds use the pinned toolchain below
# (ANOFOX_WASM_RUST_TOOLCHAIN), whose install step adds wasm32-unknown-emscripten,
# so the default toolchain need not carry that target.
if (EMSCRIPTEN)
    set(Rust_CARGO_TARGET "wasm32-unknown-emscripten")
endif()

# Fallback: detect host target from rustc when CI variables (OS_NAME/OS_ARCH) are not set
if("${Rust_CARGO_TARGET}" STREQUAL "")
    execute_process(
        COMMAND rustc --version --verbose
        OUTPUT_VARIABLE _RUSTC_VERBOSE
        ERROR_QUIET
    )
    if(_RUSTC_VERBOSE MATCHES "host: ([a-zA-Z0-9_-]+)")
        set(Rust_CARGO_TARGET "${CMAKE_MATCH_1}")
        message(STATUS "Detected Rust host target: ${Rust_CARGO_TARGET}")
    endif()
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

# WASM-only Rust toolchain pin (native builds keep the workspace floor
# rust-version = 1.89 and the rust-msrv CI job; no rust-toolchain.toml).
# Rust >= 1.87 (LLVM 20) marks wasm objects with the bulk-memory-opt and
# call-indirect-overlong target features. DuckDB CI's emsdk 3.1.71 bundles
# binaryen v120, whose `wasm-opt -O3` rejects them
# ("Unknown option '--enable-bulk-memory-opt'"), and the community-extensions
# registry's DuckDB 2.0 build uses extension-ci-tools' Rust 1.97+. So WASM builds
# pin Rust 1.86.0 through Corrosion's Rust_TOOLCHAIN. Remove the pin once DuckDB's
# emsdk understands those features. Set ANOFOX_WASM_RUST_TOOLCHAIN to "" to disable.
set(ANOFOX_WASM_RUST_TOOLCHAIN "1.86.0" CACHE STRING
    "Rust toolchain for WASM builds (empty: use the default toolchain)")
if(WASM_BUILD AND NOT "${ANOFOX_WASM_RUST_TOOLCHAIN}" STREQUAL "")
    find_program(_ANOFOX_RUSTUP rustup HINTS "$ENV{CARGO_HOME}/bin" "$ENV{HOME}/.cargo/bin")
    if(_ANOFOX_RUSTUP)
        # Idempotent: a no-op when the toolchain and target are already installed.
        execute_process(
            COMMAND "${_ANOFOX_RUSTUP}" toolchain install "${ANOFOX_WASM_RUST_TOOLCHAIN}"
                    --profile minimal --target wasm32-unknown-emscripten
            RESULT_VARIABLE _ANOFOX_RUSTUP_RESULT
            OUTPUT_VARIABLE _ANOFOX_RUSTUP_OUTPUT
            ERROR_VARIABLE _ANOFOX_RUSTUP_OUTPUT
        )
        if(NOT _ANOFOX_RUSTUP_RESULT EQUAL 0)
            message(FATAL_ERROR
                "Could not install Rust ${ANOFOX_WASM_RUST_TOOLCHAIN} for the WASM build:\n"
                "${_ANOFOX_RUSTUP_OUTPUT}\n"
                "Install it manually with\n"
                "  rustup toolchain install ${ANOFOX_WASM_RUST_TOOLCHAIN} --profile minimal "
                "--target wasm32-unknown-emscripten\n"
                "or configure with -DANOFOX_WASM_RUST_TOOLCHAIN= to build with the default toolchain.")
        endif()
        set(Rust_TOOLCHAIN "${ANOFOX_WASM_RUST_TOOLCHAIN}" CACHE STRING "Rust toolchain used by Corrosion" FORCE)
        message(STATUS "WASM build: pinned Rust toolchain ${ANOFOX_WASM_RUST_TOOLCHAIN}")
    else()
        message(WARNING "rustup not found: cannot pin Rust ${ANOFOX_WASM_RUST_TOOLCHAIN} for the WASM build; "
                        "using the default Rust toolchain.")
    endif()
endif()

# Fetch Corrosion for Rust integration
include(FetchContent)
FetchContent_Declare(
    Corrosion
    GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
    GIT_TAG v0.6.1
)
FetchContent_MakeAvailable(Corrosion)

# Import Rust FFI crate
# Note: Corrosion converts hyphens to underscores in target names
# WASM builds run on DuckDB extension-ci-tools' pinned rustc 1.86 (no override
# input), below the workspace rust-version 1.89. That floor comes from trueno's
# x86-only AVX-512 code, which the wasm32 target never compiles, so skip the
# rust-version check there only. Native builds keep enforcing 1.89 (rust-msrv CI job).
set(_ANOFOX_CARGO_FLAGS "")
if(WASM_BUILD)
    set(_ANOFOX_CARGO_FLAGS FLAGS --ignore-rust-version)
endif()
corrosion_import_crate(
    MANIFEST_PATH ${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml
    CRATES anofox-fcst-ffi
    ${_ANOFOX_CARGO_FLAGS}
)

# The Rust target name (with underscores)
set(RUST_LIB anofox_fcst_ffi)

# Set Rust build options for release
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    corrosion_set_env_vars(${RUST_LIB} "CARGO_PROFILE_RELEASE_LTO=true")
endif()

# Add PostHog telemetry library (requires OpenSSL, not available for WASM/MinGW/cross-compile)
set(TELEMETRY_SUPPORTED TRUE)
if(WASM_BUILD)
    set(TELEMETRY_SUPPORTED FALSE)
elseif(MINGW)
    set(TELEMETRY_SUPPORTED FALSE)
    message(STATUS "MinGW build - building without PostHog telemetry")
elseif(APPLE AND DEFINED OSX_BUILD_ARCH AND NOT "${OSX_BUILD_ARCH}" STREQUAL "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    set(TELEMETRY_SUPPORTED FALSE)
    message(STATUS "Cross-compiling on macOS (${OSX_BUILD_ARCH} on ${CMAKE_HOST_SYSTEM_PROCESSOR}) - building without PostHog telemetry")
endif()

if(TELEMETRY_SUPPORTED)
    # Static OpenSSL avoids runtime dependencies on libssl.so.1.1 (Linux, issue #211)
    # and libssl-3-x64.dll / libcrypto-3-x64.dll (Windows, issue #215). On Windows this
    # requires `openssl` in vcpkg.json so the x64-windows-static-release triplet
    # supplies static libs built with /MT (matching the extension's CRT).
    set(OPENSSL_USE_STATIC_LIBS ON)
    find_package(OpenSSL QUIET)
    if(OPENSSL_FOUND)
        set(DUCKDB_SRC_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/duckdb/src/include")
        set(DUCKDB_THIRD_PARTY "${CMAKE_CURRENT_SOURCE_DIR}/duckdb/third_party")
        add_subdirectory(posthog-telemetry)
        include_directories(posthog-telemetry/include)
        add_definitions(-DHAS_POSTHOG_TELEMETRY)
    else()
        # Also gates DATAZOO_BANNER_TELEMETRY below; otherwise the banner calls
        # PostHogTelemetry symbols that were never built (undefined at link).
        set(TELEMETRY_SUPPORTED FALSE)
        message(STATUS "OpenSSL not found - building without PostHog telemetry")
    endif()
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
    src/table_functions/ts_gaps.cpp
    src/table_functions/ts_fill_gaps_native.cpp
    src/table_functions/ts_fill_forward_native.cpp
    src/table_functions/ts_fill_forward_operator.cpp
    src/table_functions/ts_forecast_native.cpp
    src/table_functions/ts_forecast_panel_native.cpp
    src/table_functions/ts_forecast_var_native.cpp
    src/table_functions/ts_forecast_ensemble_native.cpp   # Phase 5: ENS-02
    src/table_functions/ts_ensemble_inspect_native.cpp    # Phase 6: INSP-01
    src/table_functions/ts_cv_split_native.cpp
    src/table_functions/ts_cv_forecast_native.cpp
    src/table_functions/ts_cv_folds_native.cpp
    src/table_functions/ts_cv_hydrate_native.cpp
    src/table_functions/ts_mstl_decomposition_native.cpp
    src/table_functions/ts_features_native.cpp
    src/table_functions/ts_aggregate_hierarchy.cpp
    src/table_functions/ts_combine_keys.cpp
    src/table_functions/ts_split_keys.cpp
    src/table_functions/ts_validate_separator.cpp
    src/table_functions/ts_metrics_native.cpp
    src/table_functions/ts_periods.cpp
    src/table_functions/ts_peaks.cpp
    src/table_functions/ts_detrend.cpp
    src/scalar_functions/metrics.cpp
    src/scalar_functions/conformal.cpp
    src/scalar_functions/bootstrap.cpp
    src/scalar_functions/diagnostics.cpp
    src/scalar_functions/ts_forecast_scalar.cpp
    src/scalar_functions/ts_forecast_inspect_scalar.cpp
    src/scalar_functions/series_transforms.cpp
    src/aggregate_functions/ts_forecast_agg.cpp
    src/aggregate_functions/ts_changepoints_agg.cpp
    src/aggregate_functions/ts_features_agg.cpp
    src/aggregate_functions/ts_classify_seasonality_agg.cpp
    src/aggregate_functions/ts_stats_agg.cpp
    src/aggregate_functions/ts_data_quality_agg.cpp
    src/aggregate_functions/ts_detect_periods_agg.cpp
    src/macros/ts_macros.cpp
)

# Build the static extension
# Feedback banner (header-only). Its banner_shown event goes through
# posthog-telemetry, so the flag follows TELEMETRY_SUPPORTED -- set after that
# variable is computed, since reading it earlier yields an empty value and would
# silently disable the event everywhere.
set(DATAZOO_BANNER_TELEMETRY ${TELEMETRY_SUPPORTED} CACHE BOOL "" FORCE)
set(DATAZOO_BANNER_TELEMETRY_INCLUDE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/posthog-telemetry/include CACHE PATH "" FORCE)
add_subdirectory(datazoo-banner)
# This repo puts dependency headers on the global include path rather than
# relying on target propagation (see posthog-telemetry above), and the extension
# targets are created by DuckDB's macros in a way that does not pick up the
# INTERFACE include from target_link_libraries here. Match the local convention.
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/datazoo-banner/include)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})

# Build the loadable extension
build_loadable_extension(${TARGET_NAME} "" ${EXTENSION_SOURCES})

# Name the targets explicitly. This file never sets EXTENSION_NAME; DuckDB
# v2.0.0 defines it as the bare extension name ("anofox_forecast", not a
# target) before add_subdirectory, so ${EXTENSION_NAME} fails configure.
target_link_libraries(${TARGET_NAME}_extension datazoo_banner)
target_link_libraries(${TARGET_NAME}_loadable_extension datazoo_banner)

# Telemetry library + OpenSSL (only when OpenSSL was found)
set(TELEMETRY_LIBS "")
if(TELEMETRY_SUPPORTED AND OPENSSL_FOUND)
    set(TELEMETRY_LIBS posthog_telemetry ${OPENSSL_LIBRARIES})
endif()

# Link Rust library, telemetry, and platform-specific libs
if(WASM_BUILD)
    # WASM: minimal linking, no system libraries, no telemetry
    target_link_libraries(${TARGET_NAME}_extension ${RUST_LIB})
    target_link_libraries(${TARGET_NAME}_loadable_extension ${RUST_LIB})
elseif(UNIX AND NOT APPLE)
    target_link_libraries(${TARGET_NAME}_extension ${RUST_LIB} ${TELEMETRY_LIBS} pthread dl m)
    target_link_libraries(${TARGET_NAME}_loadable_extension ${RUST_LIB} ${TELEMETRY_LIBS} pthread dl m)
elseif(WIN32)
    target_link_libraries(${TARGET_NAME}_extension ${RUST_LIB} ${TELEMETRY_LIBS} bcrypt)
    target_link_libraries(${TARGET_NAME}_loadable_extension ${RUST_LIB} ${TELEMETRY_LIBS} bcrypt)
elseif(APPLE)
    target_link_libraries(${TARGET_NAME}_extension ${RUST_LIB} ${TELEMETRY_LIBS} "-framework CoreFoundation" "-framework SystemConfiguration")
    target_link_libraries(${TARGET_NAME}_loadable_extension ${RUST_LIB} ${TELEMETRY_LIBS} "-framework CoreFoundation" "-framework SystemConfiguration")
else()
    target_link_libraries(${TARGET_NAME}_extension ${RUST_LIB} ${TELEMETRY_LIBS})
    target_link_libraries(${TARGET_NAME}_loadable_extension ${RUST_LIB} ${TELEMETRY_LIBS})
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
