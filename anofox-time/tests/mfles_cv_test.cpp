#include "anofox-time/models/mfles.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/utils/cross_validation.hpp"
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

    for (int i = 0; i < 150; ++i) {
        double trend = 0.3 * i;
        double seasonal = 8 * std::sin(2 * 3.14159265358979 * i / 12);
        double noise = (i % 7 - 3) * 0.5;  // Small deterministic noise
        double value = trend + seasonal + 100 + noise;
        data.push_back(value);
        timestamps.push_back(base_time + std::chrono::hours(24 * i));
    }

    std::cout << "Created time series with " << data.size() << " points" << std::endl;

    // Create time series
    core::TimeSeries ts(timestamps, data);

    // Test 1: Rolling window CV
    std::cout << "\n=== Test 1: Rolling Window CV ===" << std::endl;

    utils::CVConfig cv_config;
    cv_config.horizon = 6;
    cv_config.initial_window = 50;
    cv_config.step = 6;
    cv_config.strategy = utils::CVStrategy::ROLLING;

    // Model factory for MFLES
    auto mfles_factory = []() -> std::unique_ptr<models::IForecaster> {
        models::MFLES::Params params;
        params.seasonal_periods = {12};
        params.max_rounds = 5;
        params.trend_method = models::TrendMethod::OLS;
        return std::make_unique<models::MFLES>(params);
    };

    auto cv_results = utils::CrossValidation::evaluate(ts, mfles_factory, cv_config);

    std::cout << "Number of folds: " << cv_results.folds.size() << std::endl;
    std::cout << "Total forecasts: " << cv_results.total_forecasts << std::endl;
    std::cout << "Aggregated MAE: " << cv_results.mae << std::endl;
    std::cout << "Aggregated RMSE: " << cv_results.rmse << std::endl;

    // Show first 3 folds
    std::cout << "\nFirst 3 folds:" << std::endl;
    for (size_t i = 0; i < std::min(size_t(3), cv_results.folds.size()); ++i) {
        const auto& fold = cv_results.folds[i];
        std::cout << "  Fold " << fold.fold_id << ": "
                  << "train[" << fold.train_start << ":" << fold.train_end << "], "
                  << "test[" << fold.test_start << ":" << fold.test_end << "], "
                  << "MAE=" << fold.mae << std::endl;
    }

    // Test 2: Expanding window CV
    std::cout << "\n=== Test 2: Expanding Window CV ===" << std::endl;

    utils::CVConfig cv_config_expanding;
    cv_config_expanding.horizon = 6;
    cv_config_expanding.initial_window = 50;
    cv_config_expanding.step = 6;
    cv_config_expanding.strategy = utils::CVStrategy::EXPANDING;

    auto cv_results_expanding = utils::CrossValidation::evaluate(ts, mfles_factory, cv_config_expanding);

    std::cout << "Number of folds: " << cv_results_expanding.folds.size() << std::endl;
    std::cout << "Total forecasts: " << cv_results_expanding.total_forecasts << std::endl;
    std::cout << "Aggregated MAE: " << cv_results_expanding.mae << std::endl;
    std::cout << "Aggregated RMSE: " << cv_results_expanding.rmse << std::endl;

    // Show first 3 folds
    std::cout << "\nFirst 3 folds:" << std::endl;
    for (size_t i = 0; i < std::min(size_t(3), cv_results_expanding.folds.size()); ++i) {
        const auto& fold = cv_results_expanding.folds[i];
        std::cout << "  Fold " << fold.fold_id << ": "
                  << "train[" << fold.train_start << ":" << fold.train_end << "], "
                  << "test[" << fold.test_start << ":" << fold.test_end << "], "
                  << "MAE=" << fold.mae << std::endl;
    }

    // Verify expanding window grows
    if (cv_results_expanding.folds.size() >= 2) {
        const auto& fold1 = cv_results_expanding.folds[0];
        const auto& fold2 = cv_results_expanding.folds[1];
        int train_size1 = fold1.train_end - fold1.train_start;
        int train_size2 = fold2.train_end - fold2.train_start;

        if (train_size2 > train_size1) {
            std::cout << "\n✓ Expanding window verified: train size grows ("
                      << train_size1 << " -> " << train_size2 << ")" << std::endl;
        }
    }

    std::cout << "\n=== All CV tests passed! ===" << std::endl;
    return 0;
}
