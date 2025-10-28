#include "anofox-time/validation.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace anofoxtime::validation {

utils::AccuracyMetrics accuracyMetrics(const std::vector<double>& actual,
                                       const std::vector<double>& predicted,
                                       const std::optional<std::vector<double>>& baseline) {
    if (actual.size() != predicted.size() || actual.empty()) {
        throw std::invalid_argument("Actual and predicted vectors must be non-empty and equal length.");
    }

    utils::AccuracyMetrics metrics;
    metrics.n = actual.size();
    metrics.mae = utils::Metrics::mae(actual, predicted);
    metrics.mse = utils::Metrics::mse(actual, predicted);
    metrics.rmse = utils::Metrics::rmse(actual, predicted);
    metrics.mape = utils::Metrics::mape(actual, predicted);
    metrics.smape = utils::Metrics::smape(actual, predicted);
    if (baseline) {
        metrics.mase = utils::Metrics::mase(actual, predicted, *baseline);
    }
    metrics.r_squared = utils::Metrics::r2(actual, predicted);
    return metrics;
}

utils::AccuracyMetrics accuracyMetrics(const std::vector<std::vector<double>>& actual,
                                       const std::vector<std::vector<double>>& predicted,
                                       const std::optional<std::vector<std::vector<double>>>& baseline) {
    if (actual.empty()) {
        throw std::invalid_argument("Actual data must contain at least one dimension.");
    }
    if (actual.size() != predicted.size()) {
        throw std::invalid_argument("Actual and predicted dimension counts must match.");
    }

    const std::size_t dimensions = actual.size();
    const std::size_t horizon = predicted.front().size();

    for (std::size_t dim = 0; dim < dimensions; ++dim) {
        if (predicted[dim].size() != horizon || actual[dim].size() != horizon) {
            throw std::invalid_argument("All dimensions must share a consistent horizon.");
        }
    }

    std::optional<std::vector<double>> baseline_primary;
    if (baseline) {
        if (baseline->size() != dimensions) {
            throw std::invalid_argument("Baseline dimensions must match actual dimensions.");
        }
        baseline_primary = (*baseline)[0];
    }

    utils::AccuracyMetrics aggregate = accuracyMetrics(actual.front(), predicted.front(), baseline_primary);
    aggregate.per_dimension.reserve(dimensions);

    for (std::size_t dim = 0; dim < dimensions; ++dim) {
        std::optional<std::vector<double>> baseline_dim;
        if (baseline) {
            baseline_dim = (*baseline)[dim];
            if (baseline_dim->size() != actual[dim].size()) {
                throw std::invalid_argument("Baseline horizon must match actual horizon for each dimension.");
            }
        }

        utils::AccuracyMetrics dim_metrics = accuracyMetrics(actual[dim], predicted[dim], baseline_dim);
        dim_metrics.per_dimension.clear();
        aggregate.per_dimension.push_back(std::move(dim_metrics));
    }

    return aggregate;
}

SplitResult timeSplit(const std::vector<double>& data, double train_ratio) {
    if (data.empty()) {
        throw std::invalid_argument("Cannot split empty data.");
    }
    if (train_ratio <= 0.0 || train_ratio >= 1.0) {
        throw std::invalid_argument("Train ratio must be between 0 and 1 (exclusive).");
    }

    const std::size_t split_index = static_cast<std::size_t>(std::ceil(train_ratio * data.size()));
    if (split_index == 0 || split_index >= data.size()) {
        throw std::invalid_argument("Train ratio results in empty train or test set.");
    }

    SplitResult result;
    result.train.assign(data.begin(), data.begin() + split_index);
    result.test.assign(data.begin() + split_index, data.end());
    return result;
}

std::vector<SplitResult> timeSeriesCV(const std::vector<double>& data,
                                      std::size_t folds,
                                      std::size_t min_train,
                                      std::size_t horizon) {
    if (data.empty()) {
        throw std::invalid_argument("Cannot perform cross-validation on empty data.");
    }
    if (folds == 0) {
        throw std::invalid_argument("Number of folds must be at least 1.");
    }
    if (min_train == 0) {
        throw std::invalid_argument("Minimum training window must be positive.");
    }
    if (horizon == 0) {
        throw std::invalid_argument("Forecast horizon must be positive.");
    }

    std::vector<SplitResult> splits;

    const std::size_t max_splits = data.size() > min_train ? data.size() - min_train : 0;
    if (max_splits < horizon) {
        throw std::invalid_argument("Not enough data to create validation folds with requested parameters.");
    }

    const std::size_t step = std::max<std::size_t>(1, max_splits / folds);

    for (std::size_t start = min_train; start + horizon <= data.size(); start += step) {
        SplitResult split;
        split.train.assign(data.begin(), data.begin() + start);
        split.test.assign(data.begin() + start, data.begin() + std::min(data.size(), start + horizon));

        if (split.test.size() < horizon) {
            break;
        }

        splits.push_back(std::move(split));
        if (splits.size() == folds) {
            break;
        }
    }

    if (splits.empty()) {
        throw std::runtime_error("Cross-validation did not yield any splits. Adjust parameters or data length.");
    }

    return splits;
}

SeriesSplit timeSplit(const core::TimeSeries& series, double train_ratio) {
    if (series.isEmpty()) {
        throw std::invalid_argument("Cannot split an empty time series.");
    }
    if (train_ratio <= 0.0 || train_ratio >= 1.0) {
        throw std::invalid_argument("Train ratio must be between 0 and 1 (exclusive).");
    }

    const std::size_t split_index = static_cast<std::size_t>(std::ceil(train_ratio * static_cast<double>(series.size())));
    if (split_index == 0 || split_index >= series.size()) {
        throw std::invalid_argument("Train ratio results in empty train or test series.");
    }

    SeriesSplit split{series.slice(0, split_index), series.slice(split_index, series.size())};
    return split;
}

std::vector<SeriesSplit> rollingWindowCV(const core::TimeSeries& series, const RollingCVConfig& config) {
    if (series.isEmpty()) {
        throw std::invalid_argument("Cannot perform cross-validation on an empty time series.");
    }
    if (config.min_train == 0) {
        throw std::invalid_argument("Minimum training window must be positive.");
    }
    if (config.horizon == 0) {
        throw std::invalid_argument("Validation horizon must be positive.");
    }
    if (config.step == 0) {
        throw std::invalid_argument("Step size must be positive.");
    }
    if (config.max_folds == 0) {
        throw std::invalid_argument("Maximum folds must be positive.");
    }
    if (series.size() < config.min_train + config.horizon) {
        throw std::invalid_argument("Time series is too short for the requested configuration.");
    }

    std::vector<SeriesSplit> splits;
    splits.reserve(config.max_folds);

    for (std::size_t fold = 0;; ++fold) {
        const std::size_t train_end = config.min_train + fold * config.step;
        if (train_end + config.horizon > series.size()) {
            break;
        }

        std::size_t train_start = 0;
        if (!config.expanding && train_end > config.min_train) {
            train_start = train_end - config.min_train;
        }

        SeriesSplit split{series.slice(train_start, train_end), series.slice(train_end, train_end + config.horizon)};
        splits.push_back(std::move(split));

        if (splits.size() >= config.max_folds) {
            break;
        }
    }

    if (splits.empty()) {
        throw std::runtime_error("Cross-validation did not yield any splits. Adjust configuration or data length.");
    }

    return splits;
}

RollingBacktestSummary rollingBacktest(
    const core::TimeSeries& series,
    const RollingCVConfig& config,
    const std::function<std::unique_ptr<models::IForecaster>()>& model_factory,
    const std::function<std::optional<std::vector<double>>(const core::TimeSeries&, const core::TimeSeries&)>& baseline_provider) {
    if (!model_factory) {
        throw std::invalid_argument("Model factory must be provided for rolling backtest.");
    }

    const auto splits = rollingWindowCV(series, config);

    RollingBacktestSummary summary;
    summary.folds.reserve(splits.size());

    std::vector<double> aggregate_actual;
    std::vector<double> aggregate_predicted;
    std::vector<double> aggregate_baseline;
    bool baseline_available = true;

    for (std::size_t index = 0; index < splits.size(); ++index) {
        const auto& split = splits[index];
        auto model = model_factory();
        if (!model) {
            throw std::runtime_error("Model factory returned null forecaster instance.");
        }

        model->fit(split.train);

        const auto horizon = static_cast<int>(split.test.size());
        auto forecast = model->predict(horizon);
        const auto& predicted_ref = forecast.primary();
        const auto& actual = split.test.getValues();

        if (predicted_ref.size() != actual.size()) {
            throw std::runtime_error("Forecast horizon does not match test window length.");
        }

        std::vector<double> predicted(predicted_ref.begin(), predicted_ref.end());

        std::optional<std::vector<double>> baseline;
        if (baseline_provider) {
            baseline = baseline_provider(split.train, split.test);
            if (baseline && baseline->size() != actual.size()) {
                throw std::invalid_argument("Baseline provider must return a vector matching the validation horizon.");
            }
        } else {
            baseline_available = false;
        }

        baseline_available = baseline_available && (!baseline_provider || baseline.has_value());

        utils::AccuracyMetrics metrics = accuracyMetrics(actual, predicted, baseline);

        RollingBacktestFold fold;
        fold.index = index;
        fold.train_size = split.train.size();
        fold.test_size = split.test.size();
        fold.metrics = metrics;
        fold.forecast = std::move(forecast);
        summary.folds.push_back(std::move(fold));

        aggregate_actual.insert(aggregate_actual.end(), actual.begin(), actual.end());
        aggregate_predicted.insert(aggregate_predicted.end(), predicted.begin(), predicted.end());
        if (baseline && baseline_available) {
            aggregate_baseline.insert(aggregate_baseline.end(), baseline->begin(), baseline->end());
        }
    }

    std::optional<std::vector<double>> aggregate_baseline_opt;
    if (baseline_available && !aggregate_baseline.empty()) {
        aggregate_baseline_opt = aggregate_baseline;
    }

    summary.aggregate = accuracyMetrics(aggregate_actual, aggregate_predicted, aggregate_baseline_opt);
    return summary;
}

} // namespace anofoxtime::validation
