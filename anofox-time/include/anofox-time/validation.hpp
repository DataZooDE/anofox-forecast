#pragma once

#include "anofox-time/core/time_series.hpp"
#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/utils/metrics.hpp"
#include <optional>
#include <functional>
#include <vector>

namespace anofoxtime::validation {

utils::AccuracyMetrics accuracyMetrics(const std::vector<double>& actual,
                                       const std::vector<double>& predicted,
                                       const std::optional<std::vector<double>>& baseline = std::nullopt);

utils::AccuracyMetrics accuracyMetrics(const std::vector<std::vector<double>>& actual,
                                       const std::vector<std::vector<double>>& predicted,
                                       const std::optional<std::vector<std::vector<double>>>& baseline = std::nullopt);

struct SplitResult {
    std::vector<double> train;
    std::vector<double> test;
};

struct SeriesSplit {
    core::TimeSeries train;
    core::TimeSeries test;
};

struct RollingCVConfig {
    std::size_t min_train = 10;
    std::size_t horizon = 1;
    std::size_t step = 1;
    std::size_t max_folds = 5;
    bool expanding = true;
};

struct RollingBacktestFold {
    std::size_t index = 0;
    std::size_t train_size = 0;
    std::size_t test_size = 0;
    core::Forecast forecast;
    utils::AccuracyMetrics metrics;
};

struct RollingBacktestSummary {
    std::vector<RollingBacktestFold> folds;
    utils::AccuracyMetrics aggregate;
};

SplitResult timeSplit(const std::vector<double>& data, double train_ratio);
std::vector<SplitResult> timeSeriesCV(const std::vector<double>& data,
                                      std::size_t folds,
                                      std::size_t min_train = 10,
                                      std::size_t horizon = 1);

SeriesSplit timeSplit(const core::TimeSeries& series, double train_ratio);
std::vector<SeriesSplit> rollingWindowCV(const core::TimeSeries& series, const RollingCVConfig& config);
RollingBacktestSummary rollingBacktest(
    const core::TimeSeries& series,
    const RollingCVConfig& config,
    const std::function<std::unique_ptr<models::IForecaster>()>& model_factory,
    const std::function<std::optional<std::vector<double>>(const core::TimeSeries&, const core::TimeSeries&)>& baseline_provider = {});

} // namespace anofoxtime::validation
