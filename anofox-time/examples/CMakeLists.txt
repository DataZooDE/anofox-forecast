cmake_minimum_required(VERSION 3.21)

# Examples for anofox-time
project(anofox-time-examples LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find the anofox-time library
if(NOT TARGET anofox-time)
    # If building standalone, find the installed library
    find_library(ANOFOX_TIME_LIB anofox-time REQUIRED)
    find_path(ANOFOX_TIME_INCLUDE anofox-time/core/time_series.hpp REQUIRED)
    
    add_library(anofox-time INTERFACE)
    target_link_libraries(anofox-time INTERFACE ${ANOFOX_TIME_LIB})
    target_include_directories(anofox-time INTERFACE ${ANOFOX_TIME_INCLUDE})
    
    # Find dependencies
    find_package(spdlog CONFIG REQUIRED)
    find_package(Eigen3 CONFIG REQUIRED)
    target_link_libraries(anofox-time INTERFACE spdlog::spdlog Eigen3::Eigen)
else()
    message(STATUS "Using anofox-time target from parent project")
endif()

set(EXAMPLE_SOURCES
    pipeline_forecasting.cpp
    monitoring_workflow.cpp
    auto_arima_example.cpp
    theta_example.cpp
    baseline_example.cpp
    exponential_smoothing_example.cpp
    mfles_example.cpp
    mstl_example.cpp
    tbats_example.cpp
    airpassengers_benchmark.cpp
    intermittent_example.cpp
    ensemble_example.cpp
)

foreach(example_source IN LISTS EXAMPLE_SOURCES)
    get_filename_component(example_name ${example_source} NAME_WE)
    add_executable(${example_name} ${example_source})
    target_link_libraries(${example_name} PRIVATE anofox-time)
    set_target_properties(${example_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples
    )

    if(BUILD_TESTING)
        add_test(
            NAME example-${example_name}
            COMMAND ${example_name}
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/examples
        )
    endif()
endforeach()

add_custom_target(run-examples
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}/examples ${CMAKE_COMMAND} -E env ./pipeline_forecasting
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}/examples ${CMAKE_COMMAND} -E env ./monitoring_workflow
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}/examples ${CMAKE_COMMAND} -E env ./auto_arima_example
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}/examples ${CMAKE_COMMAND} -E env ./theta_example
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}/examples ${CMAKE_COMMAND} -E env ./baseline_example
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}/examples ${CMAKE_COMMAND} -E env ./exponential_smoothing_example
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}/examples ${CMAKE_COMMAND} -E env ./mfles_example
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}/examples ${CMAKE_COMMAND} -E env ./mstl_example
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}/examples ${CMAKE_COMMAND} -E env ./tbats_example
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}/examples ${CMAKE_COMMAND} -E env ./airpassengers_benchmark
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}/examples ${CMAKE_COMMAND} -E env ./intermittent_example
    DEPENDS pipeline_forecasting monitoring_workflow auto_arima_example theta_example baseline_example exponential_smoothing_example mfles_example mstl_example tbats_example airpassengers_benchmark intermittent_example
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/examples
    COMMENT "Running consolidated C++ examples"
)
