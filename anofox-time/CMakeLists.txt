cmake_minimum_required(VERSION 3.21)

project(anofox-time LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Vcpkg Integration ---
# Dependencies are installed from vcpkg.json.
# The toolchain file should be specified on the command line:
# cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake

# --- Project Structure ---
add_library(anofox-time
    src/utils/logging.cpp
    src/utils/metrics.cpp
    src/utils/intermittent_utils.cpp
    src/utils/robust_regression.cpp
    src/utils/cross_validation.cpp
    src/features/feature_math.cpp
    src/features/feature_registry.cpp
    src/features/feature_calculators.cpp
    src/validation.cpp
    src/selectors/auto_selector.cpp
    src/models/garch.cpp
    src/seasonality/detector.cpp
    src/seasonality/analyzer.cpp
    src/seasonality/stl.cpp
    src/seasonality/mstl.cpp
    src/clustering/dbscan.cpp
    src/outlier/dbscan_outlier.cpp
    src/models/sma.cpp
    src/models/ses.cpp
    src/models/dtw.cpp
    src/models/holt.cpp
    src/models/ets.cpp
    src/models/ets_core_statsforecast.cpp
    src/models/auto_ets.cpp
    src/utils/nelder_mead.cpp
    src/optimization/lbfgs_optimizer.cpp
    src/optimization/ets_gradients.cpp
    src/optimization/ets_gradients_simd.cpp
    src/optimization/ets_gradients_checkpointing.cpp
    src/optimization/theta_gradients.cpp
    src/detectors/mad.cpp
    src/models/arima.cpp
    src/models/auto_arima.cpp
    src/models/theta_pegels.cpp
    src/models/theta.cpp
    src/models/theta_utils.cpp
    src/models/optimized_theta.cpp
    src/models/dynamic_theta.cpp
    src/models/dynamic_optimized_theta.cpp
    src/models/auto_theta.cpp
    src/models/naive.cpp
    src/models/random_walk_drift.cpp
    src/models/seasonal_naive.cpp
    src/models/seasonal_window_average.cpp
    src/models/ses_optimized.cpp
    src/models/seasonal_es.cpp
    src/models/seasonal_es_optimized.cpp
    src/models/holt_winters.cpp
    src/models/mfles.cpp
    src/models/auto_mfles.cpp
    src/models/method_name_wrapper.cpp
    src/models/mstl_forecaster.cpp
    src/models/auto_mstl.cpp
    src/models/tbats.cpp
    src/models/auto_tbats.cpp
    src/models/croston_classic.cpp
    src/models/croston_optimized.cpp
    src/models/croston_sba.cpp
    src/models/tsb.cpp
    src/models/adida.cpp
    src/models/imapa.cpp
    src/models/ensemble.cpp
    src/changepoint/bocpd.cpp
    src/transform/pipeline.cpp
    src/transform/transformers.cpp
)
target_include_directories(anofox-time PUBLIC include)
target_include_directories(anofox-time PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/LBFGSpp/include)
set_target_properties(anofox-time PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Enable AVX2 for SIMD optimizations (if supported)
# DISABLED: Causing compatibility issues with older GCC/Eigen3
# include(CheckCXXCompilerFlag)
# check_cxx_compiler_flag("-mavx2" COMPILER_SUPPORTS_AVX2)
# if(COMPILER_SUPPORTS_AVX2)
#     target_compile_options(anofox-time PRIVATE -mavx2 -mfma)
#     message(STATUS "AVX2 support enabled for SIMD optimizations")
# endif()
message(STATUS "AVX2 support DISABLED for compatibility")

# --- Dependencies ---
find_package(spdlog CONFIG)
find_package(Threads REQUIRED)
find_package(Eigen3 CONFIG REQUIRED)

if(spdlog_FOUND)
    target_link_libraries(anofox-time PUBLIC 
        spdlog::spdlog 
        Threads::Threads
        Eigen3::Eigen
    )
else()
    message(STATUS "spdlog not found, building without logging support")
    target_link_libraries(anofox-time PUBLIC 
        Threads::Threads
        Eigen3::Eigen
    )
    target_compile_definitions(anofox-time PUBLIC ANOFOX_NO_LOGGING)
endif()

# --- Logging Control ---
option(ENABLE_LOGGING "Enable debug/info logging output (disable for production)" ON)

if(NOT ENABLE_LOGGING)
    message(STATUS "Logging disabled - compiling with ANOFOX_NO_LOGGING")
    target_compile_definitions(anofox-time PUBLIC ANOFOX_NO_LOGGING)
endif()

# --- Examples ---
option(BUILD_EXAMPLES "Build examples" ON)

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# --- Testing with Catch2 ---
enable_testing()
include(CTest)
if(BUILD_TESTING)
    find_package(Catch2 CONFIG REQUIRED)

    set(TEST_SOURCES
        tests/main.cpp
        tests/utils/test_metrics.cpp
        tests/utils/test_logging.cpp
        tests/utils/test_robust_regression.cpp
        tests/utils/test_cross_validation.cpp
        tests/utils/test_intermittent_utils.cpp
        tests/utils/test_nelder_mead.cpp
        tests/core/test_distance_matrix.cpp
        tests/core/test_time_series.cpp
        tests/core/test_forecast.cpp
        tests/validation/test_validation.cpp
        tests/models/test_sma.cpp
        tests/models/test_ses.cpp
        tests/models/test_holt.cpp
        tests/models/test_ets.cpp
        tests/models/test_auto_ets.cpp
        tests/models/test_dtw.cpp
        tests/models/test_arima.cpp
        tests/models/test_auto_arima.cpp
        tests/models/test_sarima.cpp
        tests/models/test_theta.cpp
        tests/models/test_baseline.cpp
        tests/models/test_exponential_smoothing.cpp
        tests/models/test_model_edge_cases.cpp
        tests/models/test_method_name_wrapper.cpp
        tests/models/test_mfles.cpp
        tests/models/test_auto_mfles.cpp
        tests/models/test_mstl_forecaster.cpp
        tests/models/test_auto_mstl.cpp
        tests/models/test_tbats.cpp
        tests/models/test_auto_tbats.cpp
        tests/models/test_intermittent.cpp
        tests/models/test_garch.cpp
        tests/models/test_ensemble.cpp
        tests/clustering/test_dbscan.cpp
        tests/outlier/test_dbscan_outlier.cpp
        tests/seasonality/test_detector.cpp
        tests/seasonality/test_stl.cpp
        tests/seasonality/test_mstl.cpp
        tests/changepoint/test_bocpd.cpp
        tests/transform/test_transforms.cpp
        tests/selectors/test_auto_selector.cpp
        tests/optimization/test_theta_gradients.cpp
        tests/optimization/test_ets_gradients.cpp
        # tests/integration/test_quick_pipeline.cpp  # Disabled: depends on transform classes
        tests/integration/test_monitoring_workflow.cpp
        tests/features/test_tsfresh_features.cpp
    )

    add_executable(anofox-time-tests
        ${TEST_SOURCES}
    )
    target_link_libraries(anofox-time-tests PRIVATE anofox-time Catch2::Catch2WithMain)
    target_compile_definitions(anofox-time-tests PRIVATE ANOFOX_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_include_directories(anofox-time-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)

    include(Catch)
    catch_discover_tests(
        anofox-time-tests
        TEST_PREFIX "anofox-time."
        EXTRA_ARGS --success --durations yes
    )
endif()
