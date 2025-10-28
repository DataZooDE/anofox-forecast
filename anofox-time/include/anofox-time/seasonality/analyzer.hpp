#pragma once

#include "anofox-time/core/time_series.hpp"
#include "anofox-time/seasonality/detector.hpp"
#include "anofox-time/seasonality/stl.hpp"
#include <optional>
#include <vector>

namespace anofoxtime::seasonality {

struct SeasonalityComponents {
    std::vector<double> trend;
    std::vector<std::vector<double>> seasonals;
    std::vector<double> remainder;

    [[nodiscard]] std::vector<double> aggregateSeasonal() const;
};

struct SeasonalityAnalysis {
    std::vector<std::uint32_t> detected_periods;
    std::optional<std::uint32_t> selected_period; // first (primary) period when available
    SeasonalityComponents components;
    double seasonal_strength = 0.0;
    double trend_strength = 0.0;
};

class SeasonalityAnalyzer {
public:
    explicit SeasonalityAnalyzer(SeasonalityDetector detector);

    SeasonalityAnalysis analyze(const core::TimeSeries& ts,
                                std::optional<std::size_t> override_period = std::nullopt) const;

private:
    SeasonalityDetector detector_;
};

} // namespace anofoxtime::seasonality
