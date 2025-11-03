#include "anofox-time/models/auto_mfles.hpp"
#include "anofox-time/core/time_series.hpp"
#include <iostream>
#include <vector>
#include <cmath>

int main() {
    using namespace anofoxtime;
    using TimePoint = std::chrono::system_clock::time_point;

    // Create test data with trend + seasonality
    std::vector<double> data;
    std::vector<TimePoint> timestamps;
    auto base_time = std::chrono::system_clock::now();

    for (int i = 0; i < 120; ++i) {
        double trend = 0.4 * i;
        double seasonal = 10 * std::sin(2 * 3.14159265358979 * i / 12);
        double noise = (i % 5 - 2) * 0.3;
        double value = trend + seasonal + 100 + noise;
        data.push_back(value);
        timestamps.push_back(base_time + std::chrono::hours(24 * i));
    }

    std::cout << "Created time series with " << data.size() << " points" << std::endl;

    // Create time series
    core::TimeSeries ts(timestamps, data);

    // Test AutoMFLES with limited search space for faster testing
    std::cout << "\n=== Test: AutoMFLES with CV optimization ===" << std::endl;

    models::AutoMFLES::Config config;
    config.cv_horizon = 6;
    config.cv_initial_window = 50;
    config.cv_step = 10;  // Larger step for fewer folds (faster)
    config.cv_strategy = utils::CVStrategy::ROLLING;

    // Limit search space for faster testing
    config.trend_methods = {
        models::TrendMethod::OLS,
        models::TrendMethod::SIEGEL_ROBUST
    };
    config.max_fourier_orders = {3, 5};
    config.max_rounds_options = {3, 5};
    config.seasonal_periods = {12};

    models::AutoMFLES auto_model(config);

    std::cout << "Fitting AutoMFLES (this will test multiple configurations)..." << std::endl;
    auto_model.fit(ts);

    std::cout << "\n=== Optimization Results ===" << std::endl;
    const auto& diag = auto_model.diagnostics();
    std::cout << "Configurations evaluated: " << diag.configs_evaluated << std::endl;
    std::cout << "Best CV MAE: " << diag.best_cv_mae << std::endl;
    std::cout << "Best trend method: ";
    switch (diag.best_trend_method) {
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
    std::cout << "Best Fourier order: " << diag.best_fourier_order << std::endl;
    std::cout << "Best max rounds: " << diag.best_max_rounds << std::endl;
    std::cout << "Optimization time: " << diag.optimization_time_ms << " ms" << std::endl;

    // Generate forecast
    std::cout << "\n=== Forecast ===" << std::endl;
    auto forecast = auto_model.predict(12);
    const auto& forecasts = forecast.primary();

    std::cout << "First 6 forecast values: ";
    for (int i = 0; i < 6; ++i) {
        std::cout << forecasts[i] << " ";
    }
    std::cout << std::endl;

    // Verify results make sense
    if (diag.configs_evaluated > 0 &&
        diag.best_cv_mae > 0 &&
        diag.best_cv_mae < std::numeric_limits<double>::infinity() &&
        forecasts.size() == 12) {
        std::cout << "\n✓ AutoMFLES optimization successful!" << std::endl;
        std::cout << "  - Evaluated " << diag.configs_evaluated << " configurations" << std::endl;
        std::cout << "  - Selected optimal hyperparameters via CV" << std::endl;
        std::cout << "  - Generated " << forecasts.size() << " forecasts" << std::endl;
    } else {
        std::cerr << "\n✗ AutoMFLES test failed!" << std::endl;
        return 1;
    }

    std::cout << "\n=== All AutoMFLES tests passed! ===" << std::endl;
    return 0;
}
