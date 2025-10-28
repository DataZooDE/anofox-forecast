#include "anofox-time/seasonality/analyzer.hpp"
#include "anofox-time/seasonality/mstl.hpp"
#include "anofox-time/utils/logging.hpp"
#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace {

double variance(const std::vector<double>& values) {
    if (values.empty()) return 0.0;
    const double mean = std::accumulate(values.begin(), values.end(), 0.0) /
                        static_cast<double>(values.size());
    double accum = 0.0;
    for (double v : values) {
        const double diff = v - mean;
        accum += diff * diff;
    }
    return accum / static_cast<double>(values.size());
}

} // namespace

namespace anofoxtime::seasonality {

std::vector<double> SeasonalityComponents::aggregateSeasonal() const {
    if (seasonals.empty()) {
        return {};
    }
    const std::size_t n = seasonals.front().size();
    std::vector<double> result(n, 0.0);
    for (const auto& component : seasonals) {
        if (component.size() != n) continue;
        for (std::size_t i = 0; i < n; ++i) {
            result[i] += component[i];
        }
    }
    return result;
}

SeasonalityAnalyzer::SeasonalityAnalyzer(SeasonalityDetector detector)
    : detector_(std::move(detector)) {}

SeasonalityAnalysis SeasonalityAnalyzer::analyze(const core::TimeSeries& ts,
                                                 std::optional<std::size_t> override_period) const {
    const auto& values = ts.getValues();
    auto detected = detector_.detect(values);

    std::optional<std::uint32_t> selected;
    if (override_period.has_value()) {
        selected = static_cast<std::uint32_t>(*override_period);
    } else if (!detected.empty()) {
        selected = detected.front();
    }

    SeasonalityAnalysis analysis;
    analysis.detected_periods = std::move(detected);
    analysis.selected_period = selected;

    if (!selected.has_value()) {
        throw std::runtime_error("Seasonality analysis failed: no periods detected.");
    }

    if (analysis.detected_periods.size() > 1 && !override_period.has_value()) {
        std::vector<std::size_t> periods(analysis.detected_periods.begin(), analysis.detected_periods.end());
        MSTLDecomposition mstl = MSTLDecomposition::builder()
                                     .withPeriods(std::move(periods))
                                     .withIterations(2)
                                     .withRobust(false)
                                     .build();
        mstl.fit(ts);

        const auto& comps = mstl.components();
        analysis.components.trend = comps.trend;
        analysis.components.seasonals = comps.seasonal;
        analysis.components.remainder = comps.remainder;

        auto aggregate = analysis.components.aggregateSeasonal();
        if (!aggregate.empty()) {
            std::vector<double> seasonal_plus_remainder(aggregate.size());
            for (std::size_t i = 0; i < aggregate.size(); ++i) {
                seasonal_plus_remainder[i] = aggregate[i] + analysis.components.remainder[i];
            }
            const double var_remainder = variance(analysis.components.remainder);
            const double var_total = variance(seasonal_plus_remainder);
            analysis.seasonal_strength = (var_total > 0.0) ? 1.0 - var_remainder / var_total : 0.0;
        }

        std::vector<double> trend_plus_remainder(analysis.components.trend.size());
        for (std::size_t i = 0; i < trend_plus_remainder.size(); ++i) {
            trend_plus_remainder[i] = analysis.components.trend[i] + analysis.components.remainder[i];
        }
        const double var_remainder = variance(analysis.components.remainder);
        const double var_trend_total = variance(trend_plus_remainder);
        analysis.trend_strength = (var_trend_total > 0.0) ? 1.0 - var_remainder / var_trend_total : 0.0;

        return analysis;
    }

    auto stl = STLDecomposition::builder()
                    .withPeriod(*selected)
                    .withSeasonalSmoother(*selected)
                    .withTrendSmoother(std::max<std::size_t>(*selected * 3, static_cast<std::size_t>(7)))
                    .build();
    stl.fit(ts);

    analysis.components.trend = stl.trend();
    analysis.components.seasonals = {stl.seasonal()};
    analysis.components.remainder = stl.remainder();
    analysis.seasonal_strength = stl.seasonalStrength();
    analysis.trend_strength = stl.trendStrength();

    return analysis;
}

} // namespace anofoxtime::seasonality
