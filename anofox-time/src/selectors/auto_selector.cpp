#include "anofox-time/selectors/auto_selector.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

using namespace anofoxtime;

namespace {

quick::ForecastSummary runCandidate(const selectors::CandidateModel& candidate,
                                    const std::vector<double>& train,
                                    const std::vector<double>& actual,
                                    const std::optional<std::vector<double>>& baseline) {
    const int horizon = static_cast<int>(actual.size());
    switch (candidate.type) {
        case selectors::CandidateModel::Type::SimpleMovingAverage:
            return quick::movingAverage(train, candidate.window, horizon, actual, baseline);
        case selectors::CandidateModel::Type::SimpleExponentialSmoothing:
            return quick::simpleExponentialSmoothing(train, candidate.alpha, horizon, actual, baseline);
        case selectors::CandidateModel::Type::HoltLinearTrend:
            return quick::holtLinearTrend(train, candidate.alpha, candidate.beta, horizon, actual, baseline);
        case selectors::CandidateModel::Type::ARIMA:
            return quick::arima(train, candidate.p, candidate.d, candidate.q, horizon, actual, baseline, candidate.include_intercept);
        case selectors::CandidateModel::Type::ETS: {
            quick::ETSOptions options;
            options.alpha = candidate.alpha;
            options.trend = candidate.ets_trend;
            options.season = candidate.ets_season;
            options.season_length = candidate.season_length;
            options.phi = candidate.phi;
            if (candidate.ets_trend != models::ETSTrendType::None) {
                options.beta = candidate.beta;
            }
            if (candidate.ets_season != models::ETSSeasonType::None) {
                options.gamma = candidate.gamma;
            }
            return quick::ets(train, horizon, options, actual, baseline);
        }
        default:
            throw std::invalid_argument("Unsupported candidate model type.");
    }
}

} // namespace

namespace anofoxtime::selectors {

std::string CandidateModel::description() const {
    switch (type) {
        case Type::SimpleMovingAverage:
            return "SMA(window=" + std::to_string(window) + ")";
        case Type::SimpleExponentialSmoothing:
            return "SES(alpha=" + std::to_string(alpha) + ")";
        case Type::HoltLinearTrend:
            return "Holt(alpha=" + std::to_string(alpha) + ", beta=" + std::to_string(beta) + ")";
        case Type::ARIMA:
            return "ARIMA(" + std::to_string(p) + "," + std::to_string(d) + "," + std::to_string(q) +
                   (include_intercept ? ", intercept" : ", no-intercept") + ")";
        case Type::ETS: {
            std::string description = "ETS(alpha=" + std::to_string(alpha);
            if (ets_trend != models::ETSTrendType::None) {
                description += ", beta=" + std::to_string(beta);
            }
            if (ets_season != models::ETSSeasonType::None) {
                description += ", gamma=" + std::to_string(gamma) + ", season_length=" + std::to_string(season_length);
            }
            description += ")";
            return description;
        }
        default:
            return "Unknown";
    }
}

AutoSelector::AutoSelector()
    : candidates_(defaultCandidates()), scorer_(defaultScore) {}

AutoSelector::AutoSelector(std::vector<CandidateModel> candidates)
    : candidates_(std::move(candidates)), scorer_(defaultScore) {}

AutoSelector& AutoSelector::withScoringFunction(ScoringFunction scorer) {
    if (!scorer) {
        throw std::invalid_argument("Scoring function must be callable.");
    }
    scorer_ = std::move(scorer);
    return *this;
}

AutoSelector& AutoSelector::withCandidates(std::vector<CandidateModel> candidates) {
    candidates_ = std::move(candidates);
    return *this;
}

SelectionResult AutoSelector::select(const std::vector<double>& train,
                                     const std::vector<double>& actual,
                                     const std::optional<std::vector<double>>& baseline) const {
    if (train.empty()) {
        throw std::invalid_argument("Training data must not be empty for model selection.");
    }
    if (actual.empty()) {
        throw std::invalid_argument("Actual data must not be empty for model selection.");
    }
    if (baseline && baseline->size() != actual.size()) {
        throw std::invalid_argument("Baseline size must match actual size.");
    }

    std::vector<SelectionEntry> results;
    results.reserve(candidates_.size());

    for (const auto& candidate : candidates_) {
        try {
            auto summary = runCandidate(candidate, train, actual, baseline);
            if (!summary.metrics.has_value()) {
                ANOFOX_WARN("Skipping candidate {} due to missing metrics.", candidate.description());
                continue;
            }
            const double score = scorer_(*summary.metrics);
            if (!std::isfinite(score)) {
                ANOFOX_WARN("Skipping candidate {} due to non-finite score.", candidate.description());
                continue;
            }

            SelectionEntry entry{candidate, std::move(summary), score};
            results.push_back(std::move(entry));
        } catch (const std::exception& ex) {
            ANOFOX_WARN("Candidate {} failed: {}", candidate.description(), ex.what());
        }
    }

    if (results.empty()) {
        throw std::runtime_error("No candidate models produced valid metrics.");
    }

    std::sort(results.begin(), results.end(), [](const SelectionEntry& lhs, const SelectionEntry& rhs) {
        return lhs.score < rhs.score;
    });

    SelectionResult result;
    result.best = results.front();
    result.ranked = results;
    ANOFOX_INFO("AutoSelector chose {} with score {:.6f}.", result.best.model.description(), result.best.score);
    return result;
}

SelectionResult AutoSelector::selectWithCV(const std::vector<double>& data,
                                           std::size_t folds,
                                           std::size_t min_train,
                                           std::size_t horizon) const {
    // Validate that data is sufficient for CV
    if (data.size() < min_train + horizon) {
        throw std::runtime_error("Insufficient data for cross-validation: need at least min_train + horizon points");
    }
    if (folds == 0) {
        throw std::invalid_argument("Number of folds must be positive");
    }
    
    const auto splits = validation::timeSeriesCV(data, folds, min_train, horizon);

    std::vector<SelectionEntry> aggregate;
    aggregate.reserve(candidates_.size());

    for (const auto& candidate : candidates_) {
        double total_score = 0.0;
        std::size_t valid_folds = 0;
        quick::ForecastSummary last_summary;

        for (const auto& split : splits) {
            try {
                auto summary = runCandidate(candidate, split.train, split.test, std::nullopt);
                if (!summary.metrics.has_value()) {
                    continue;
                }
                total_score += scorer_(*summary.metrics);
                ++valid_folds;
                last_summary = std::move(summary);
            } catch (const std::exception& ex) {
                ANOFOX_WARN("Candidate {} failed during CV fold: {}", candidate.description(), ex.what());
            }
        }

        if (valid_folds == 0) {
            continue;
        }

        SelectionEntry entry{candidate, std::move(last_summary), total_score / static_cast<double>(valid_folds)};
        aggregate.push_back(std::move(entry));
    }

    if (aggregate.empty()) {
        throw std::runtime_error("Cross-validation failed: no valid candidate results.");
    }

    std::sort(aggregate.begin(), aggregate.end(), [](const SelectionEntry& lhs, const SelectionEntry& rhs) {
        return lhs.score < rhs.score;
    });

    SelectionResult result;
    result.best = aggregate.front();
    result.ranked = aggregate;
    ANOFOX_INFO("AutoSelector (CV) chose {} with score {:.6f} ({} folds).",
                result.best.model.description(), result.best.score, aggregate.size());
    return result;
}

std::vector<CandidateModel> AutoSelector::defaultCandidates() {
    return {
        CandidateModel{CandidateModel::Type::SimpleMovingAverage, 3},
        CandidateModel{CandidateModel::Type::SimpleMovingAverage, 5},
        CandidateModel{CandidateModel::Type::SimpleMovingAverage, 7},
        CandidateModel{CandidateModel::Type::SimpleExponentialSmoothing, 0, 0.3},
        CandidateModel{CandidateModel::Type::SimpleExponentialSmoothing, 0, 0.5},
        CandidateModel{CandidateModel::Type::SimpleExponentialSmoothing, 0, 0.7},
        CandidateModel{CandidateModel::Type::HoltLinearTrend, 0, 0.5, 0.3},
        CandidateModel{CandidateModel::Type::HoltLinearTrend, 0, 0.8, 0.2},
        CandidateModel{CandidateModel::Type::HoltLinearTrend, 0, 0.3, 0.1},
        CandidateModel{CandidateModel::Type::ETS, 0, 0.3, 0.1, 0, 0, 0, true, models::ETSTrendType::None, models::ETSSeasonType::None, 0, 0.0, 0.98},
        CandidateModel{CandidateModel::Type::ETS, 0, 0.3, 0.1, 0, 0, 0, true, models::ETSTrendType::Additive, models::ETSSeasonType::None, 0, 0.0, 0.98},
        CandidateModel{CandidateModel::Type::ETS, 0, 0.3, 0.1, 0, 0, 0, true, models::ETSTrendType::None, models::ETSSeasonType::Additive, 12, 0.2, 0.98},
        CandidateModel{CandidateModel::Type::ARIMA, 0, 0.0, 0.0, 1, 1, 1},
        CandidateModel{CandidateModel::Type::ARIMA, 0, 0.0, 0.0, 2, 1, 1},
        CandidateModel{CandidateModel::Type::ARIMA, 0, 0.0, 0.0, 1, 1, 2}
    };
}

double AutoSelector::defaultScore(const utils::AccuracyMetrics& metrics) {
    if (std::isfinite(metrics.rmse)) {
        return metrics.rmse;
    }
    return metrics.mae;
}

} // namespace anofoxtime::selectors
