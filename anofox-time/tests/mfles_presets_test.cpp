#include "anofox-time/models/mfles.hpp"
#include "anofox-time/core/time_series.hpp"
#include <iostream>
#include <vector>
#include <cmath>
#include <chrono>

int main() {
    using namespace anofoxtime;
    using TimePoint = std::chrono::system_clock::time_point;

    // Create test data with trend + seasonality
    std::vector<double> data;
    std::vector<TimePoint> timestamps;
    auto base_time = std::chrono::system_clock::now();

    for (int i = 0; i < 120; ++i) {
        double trend = 0.5 * i;
        double seasonal = 12 * std::sin(2 * 3.14159265358979 * i / 12);
        double noise = (i % 7 - 3) * 0.5;
        double value = trend + seasonal + 100 + noise;
        data.push_back(value);
        timestamps.push_back(base_time + std::chrono::hours(24 * i));
    }

    // Add some outliers
    data[30] += 50;
    data[60] -= 40;
    data[90] += 35;

    std::cout << "Created time series with " << data.size() << " points (with outliers)" << std::endl;

    // Create time series
    core::TimeSeries ts(timestamps, data);

    // Test each preset
    std::vector<std::pair<std::string, models::MFLES::Params>> presets = {
        {"Fast", models::MFLES::fastPreset()},
        {"Balanced", models::MFLES::balancedPreset()},
        {"Accurate", models::MFLES::accuratePreset()},
        {"Robust", models::MFLES::robustPreset()}
    };

    std::cout << "\n=== Testing MFLES Configuration Presets ===" << std::endl;

    for (const auto& [name, params] : presets) {
        std::cout << "\n--- " << name << " Preset ---" << std::endl;

        // Display configuration
        std::cout << "Configuration:" << std::endl;
        std::cout << "  - Max rounds: " << params.max_rounds << std::endl;
        std::cout << "  - Fourier order: " << params.fourier_order << std::endl;
        std::cout << "  - Trend method: ";
        switch (params.trend_method) {
            case models::TrendMethod::OLS:
                std::cout << "OLS" << std::endl;
                break;
            case models::TrendMethod::SIEGEL_ROBUST:
                std::cout << "Siegel Robust" << std::endl;
                break;
            case models::TrendMethod::PIECEWISE:
                std::cout << "Piecewise" << std::endl;
                break;
        }
        std::cout << "  - ES ensemble steps: " << params.es_ensemble_steps << std::endl;
        std::cout << "  - Cap outliers: " << (params.cap_outliers ? "Yes" : "No") << std::endl;
        std::cout << "  - Seasonality weights: " << (params.seasonality_weights ? "Yes" : "No") << std::endl;

        // Fit model
        models::MFLES model(params);

        auto start = std::chrono::high_resolution_clock::now();
        model.fit(ts);
        auto end = std::chrono::high_resolution_clock::now();

        double fit_time_ms = std::chrono::duration<double, std::milli>(end - start).count();

        // Generate forecast
        auto forecast = model.predict(12);
        const auto& forecasts = forecast.primary();

        std::cout << "\nResults:" << std::endl;
        std::cout << "  - Fit time: " << fit_time_ms << " ms" << std::endl;
        std::cout << "  - Actual rounds used: " << model.actualRoundsUsed() << std::endl;
        std::cout << "  - First 3 forecasts: ";
        for (int i = 0; i < 3 && i < forecasts.size(); ++i) {
            std::cout << forecasts[i] << " ";
        }
        std::cout << std::endl;
    }

    // Verify all presets produced forecasts
    bool all_passed = true;
    for (const auto& [name, params] : presets) {
        models::MFLES model(params);
        model.fit(ts);
        auto forecast = model.predict(12);

        if (forecast.primary().size() != 12) {
            std::cerr << "\n✗ " << name << " preset failed to generate 12 forecasts!" << std::endl;
            all_passed = false;
        }

        // Check forecasts are reasonable (within 3x range of original data)
        double min_val = *std::min_element(data.begin(), data.end());
        double max_val = *std::max_element(data.begin(), data.end());
        double range = max_val - min_val;

        for (double val : forecast.primary()) {
            if (val < min_val - 2 * range || val > max_val + 2 * range) {
                std::cerr << "\n✗ " << name << " preset produced unreasonable forecast: " << val << std::endl;
                all_passed = false;
                break;
            }
        }
    }

    if (all_passed) {
        std::cout << "\n✓ All configuration presets working correctly!" << std::endl;
        std::cout << "  - Fast preset: Quick forecasting with minimal computation" << std::endl;
        std::cout << "  - Balanced preset: Recommended default configuration" << std::endl;
        std::cout << "  - Accurate preset: High accuracy with more computation" << std::endl;
        std::cout << "  - Robust preset: Maximum resistance to outliers" << std::endl;
    } else {
        std::cerr << "\n✗ Some presets failed!" << std::endl;
        return 1;
    }

    std::cout << "\n=== All preset tests passed! ===" << std::endl;
    return 0;
}
