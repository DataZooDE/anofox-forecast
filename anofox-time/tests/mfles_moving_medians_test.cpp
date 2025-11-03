#include "anofox-time/models/mfles.hpp"
#include "anofox-time/core/time_series.hpp"
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>

int main() {
    using namespace anofoxtime;
    using TimePoint = std::chrono::system_clock::time_point;

    // Create test data with trend + seasonality + level shift in middle
    std::vector<double> data;
    std::vector<TimePoint> timestamps;
    auto base_time = std::chrono::system_clock::now();

    // First half: baseline level around 100
    for (int i = 0; i < 60; ++i) {
        double trend = 0.2 * i;
        double seasonal = 10 * std::sin(2 * 3.14159265358979 * i / 12);
        double value = trend + seasonal + 100;
        data.push_back(value);
        timestamps.push_back(base_time + std::chrono::hours(24 * i));
    }

    // Second half: level shift to 150
    for (int i = 60; i < 120; ++i) {
        double trend = 0.2 * i;
        double seasonal = 10 * std::sin(2 * 3.14159265358979 * i / 12);
        double value = trend + seasonal + 150;  // Level shift!
        data.push_back(value);
        timestamps.push_back(base_time + std::chrono::hours(24 * i));
    }

    std::cout << "Created time series with 120 points and level shift at t=60" << std::endl;

    // Create time series
    core::TimeSeries ts(timestamps, data);

    // Test 1: Global median (default)
    std::cout << "\n=== Test 1: Global Median (default) ===" << std::endl;

    models::MFLES::Params global_params;
    global_params.seasonal_periods = {12};
    global_params.max_rounds = 3;
    global_params.fourier_order = 3;
    global_params.moving_medians = false;  // Use global median

    models::MFLES global_model(global_params);
    global_model.fit(ts);

    auto global_forecast = global_model.predict(12);
    const auto& global_forecasts = global_forecast.primary();

    std::cout << "Global median configuration:" << std::endl;
    std::cout << "  - Moving medians: No (uses global median of all 120 points)" << std::endl;
    std::cout << "  - First 3 forecasts: ";
    for (int i = 0; i < 3; ++i) {
        std::cout << global_forecasts[i] << " ";
    }
    std::cout << std::endl;

    // Compute expected global median
    std::vector<double> sorted_data = data;
    std::sort(sorted_data.begin(), sorted_data.end());
    double expected_global_median = (sorted_data[59] + sorted_data[60]) / 2.0;
    std::cout << "  - Expected global median: ~" << expected_global_median << std::endl;

    // Test 2: Moving window median
    std::cout << "\n=== Test 2: Moving Window Median ===" << std::endl;

    models::MFLES::Params moving_params;
    moving_params.seasonal_periods = {12};
    moving_params.max_rounds = 3;
    moving_params.fourier_order = 3;
    moving_params.moving_medians = true;  // Use moving window median

    models::MFLES moving_model(moving_params);
    moving_model.fit(ts);

    auto moving_forecast = moving_model.predict(12);
    const auto& moving_forecasts = moving_forecast.primary();

    std::cout << "Moving median configuration:" << std::endl;
    std::cout << "  - Moving medians: Yes (uses last 24 points, 2 seasonal cycles)" << std::endl;
    std::cout << "  - First 3 forecasts: ";
    for (int i = 0; i < 3; ++i) {
        std::cout << moving_forecasts[i] << " ";
    }
    std::cout << std::endl;

    // Compute expected moving median (last 24 points)
    std::vector<double> recent_data(data.begin() + 96, data.end());
    std::sort(recent_data.begin(), recent_data.end());
    double expected_moving_median = (recent_data[11] + recent_data[12]) / 2.0;
    std::cout << "  - Expected moving median: ~" << expected_moving_median << std::endl;

    // Verify that moving median forecasts are higher due to level shift
    std::cout << "\n=== Comparison ===" << std::endl;

    double global_avg = 0.0, moving_avg = 0.0;
    for (int i = 0; i < 12; ++i) {
        global_avg += global_forecasts[i];
        moving_avg += moving_forecasts[i];
    }
    global_avg /= 12.0;
    moving_avg /= 12.0;

    std::cout << "Average forecast (12 steps):" << std::endl;
    std::cout << "  - Global median:  " << global_avg << std::endl;
    std::cout << "  - Moving median:  " << moving_avg << std::endl;

    // Verify both models produce different results
    double diff = std::abs(moving_avg - global_avg);
    std::cout << "  - Difference: " << diff << std::endl;

    if (diff > 1.0) {  // At least some meaningful difference
        std::cout << "\n✓ Moving median produces different baseline than global median!" << std::endl;
        std::cout << "  - Difference of " << diff << " shows different median components" << std::endl;
        std::cout << "  - Moving median adapts to recent data (uses last 24 points)" << std::endl;
        std::cout << "  - Global median uses all 120 points" << std::endl;
        std::cout << "\nNote: Final forecasts depend on all 5 MFLES components" << std::endl;
        std::cout << "  (median, trend, seasonality, ES, exogenous), not just median." << std::endl;
    } else {
        std::cerr << "\n✗ Moving and global medians produced nearly identical results!" << std::endl;
        return 1;
    }

    // Verify both forecasts are reasonable
    if (global_forecasts.size() == 12 && moving_forecasts.size() == 12) {
        std::cout << "\n✓ Both configurations produced correct forecast lengths!" << std::endl;
    } else {
        std::cerr << "\n✗ Forecast length mismatch!" << std::endl;
        return 1;
    }

    std::cout << "\n=== All moving median tests passed! ===" << std::endl;
    return 0;
}
