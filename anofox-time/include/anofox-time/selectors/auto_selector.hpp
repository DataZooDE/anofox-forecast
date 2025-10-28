#pragma once

#include "anofox-time/quick.hpp"
#include "anofox-time/utils/logging.hpp"
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace anofoxtime::selectors {

struct CandidateModel {
    enum class Type {
        SimpleMovingAverage,
        SimpleExponentialSmoothing,
        HoltLinearTrend,
        ARIMA,
        ETS
    };

    Type type;
    int window = 0;         // SMA
    double alpha = 0.0;     // SES/Holt
    double beta = 0.0;      // Holt
    int p = 0;              // ARIMA
    int d = 0;              // ARIMA
    int q = 0;              // ARIMA
    bool include_intercept = true;
    models::ETSTrendType ets_trend = models::ETSTrendType::None;
    models::ETSSeasonType ets_season = models::ETSSeasonType::None;
    int season_length = 0;
    double gamma = 0.0;
    double phi = 0.98;

    std::string description() const;
};

struct SelectionEntry {
    CandidateModel model;
    quick::ForecastSummary summary;
    double score = std::numeric_limits<double>::infinity();
};

struct SelectionResult {
    SelectionEntry best;
    std::vector<SelectionEntry> ranked;
};

class AutoSelector {
public:
    using ScoringFunction = std::function<double(const utils::AccuracyMetrics&)>;

    AutoSelector();
    explicit AutoSelector(std::vector<CandidateModel> candidates);
    AutoSelector& withScoringFunction(ScoringFunction scorer);
    AutoSelector& withCandidates(std::vector<CandidateModel> candidates);

    SelectionResult select(const std::vector<double>& train,
                           const std::vector<double>& actual,
                           const std::optional<std::vector<double>>& baseline = std::nullopt) const;

    SelectionResult selectWithCV(const std::vector<double>& data,
                                 std::size_t folds,
                                 std::size_t min_train = 10,
                                 std::size_t horizon = 1) const;

    const std::vector<CandidateModel>& candidates() const { return candidates_; }

private:
    static std::vector<CandidateModel> defaultCandidates();
    static double defaultScore(const utils::AccuracyMetrics& metrics);

    std::vector<CandidateModel> candidates_;
    ScoringFunction scorer_;
};

} // namespace anofoxtime::selectors
