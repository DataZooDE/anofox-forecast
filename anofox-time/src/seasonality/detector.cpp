#include "anofox-time/seasonality/detector.hpp"
#include "anofox-time/utils/logging.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace {

std::uint32_t defaultMaxPeriod(const std::vector<double>& data, std::uint32_t min_period) {
    if (data.size() < static_cast<std::size_t>(min_period * 3)) {
        return static_cast<std::uint32_t>(data.size() / 2);
    }
    const double max_by_cycles = static_cast<double>(data.size()) / 3.0;
    return static_cast<std::uint32_t>(std::clamp(max_by_cycles, 6.0, 512.0));
}

} // namespace

namespace anofoxtime::seasonality {

SeasonalityDetector::SeasonalityDetector(std::uint32_t min_period,
                                         std::optional<std::uint32_t> max_period,
                                         double threshold)
    : min_period_(min_period), max_period_(max_period), threshold_(std::clamp(threshold, 0.01, 0.99)) {}

SeasonalityDetector::Builder& SeasonalityDetector::Builder::minPeriod(std::uint32_t value) {
    min_period_ = value;
    return *this;
}

SeasonalityDetector::Builder& SeasonalityDetector::Builder::maxPeriod(std::uint32_t value) {
    max_period_ = value;
    return *this;
}

SeasonalityDetector::Builder& SeasonalityDetector::Builder::threshold(double value) {
    threshold_ = std::clamp(value, 0.01, 0.99);
    return *this;
}

SeasonalityDetector SeasonalityDetector::Builder::build() const {
    return SeasonalityDetector(min_period_, max_period_, threshold_);
}

SeasonalityDetector::Builder SeasonalityDetector::builder() {
    return Builder();
}

Periodogram SeasonalityDetector::periodogram(const std::vector<double>& data) const {
    Periodogram result;
    const std::size_t n = data.size();
    if (n < static_cast<std::size_t>(min_period_ * 2)) {
        ANOFOX_WARN("Seasonality detection skipped: data length {} < {}", n, min_period_ * 2);
        return result;
    }

    const double mean = std::accumulate(data.begin(), data.end(), 0.0) / static_cast<double>(n);
    std::vector<double> centered(data.size());
    std::transform(data.begin(), data.end(), centered.begin(), [mean](double v) { return v - mean; });

    const double variance = std::inner_product(centered.begin(), centered.end(), centered.begin(), 0.0) /
                            static_cast<double>(n);
    if (variance <= 0.0) {
        ANOFOX_WARN("Seasonality detection skipped: variance is zero.");
        return result;
    }

    const std::uint32_t max_period = max_period_.value_or(defaultMaxPeriod(data, min_period_));
    const std::uint32_t upper = std::min<std::uint32_t>(max_period, static_cast<std::uint32_t>(n / 2));
    if (upper <= min_period_) {
        ANOFOX_WARN("Seasonality detection skipped: max period {} <= min period {}", upper, min_period_);
        return result;
    }

    result.periods.reserve(upper - min_period_);
    result.powers.reserve(upper - min_period_);

    for (std::uint32_t period = min_period_; period < upper; ++period) {
        double numerator = 0.0;
        for (std::size_t i = period; i < n; ++i) {
            numerator += centered[i] * centered[i - period];
        }
        const double denom = static_cast<double>(n - period) * variance;
        const double corr = denom > 0.0 ? numerator / denom : 0.0;
        result.periods.push_back(period);
        result.powers.push_back(std::abs(corr));
    }

    return result;
}

std::vector<std::uint32_t> SeasonalityDetector::detect(const std::vector<double>& data,
                                                       std::size_t max_peaks) const {
    auto pg = periodogram(data);
    std::vector<std::uint32_t> periods;
    if (pg.periods.empty()) {
        return periods;
    }
    auto peaks = pg.peaks(threshold_);
    std::vector<PeriodogramPeak> sorted_peaks(peaks.begin(), peaks.end());
    std::sort(sorted_peaks.begin(), sorted_peaks.end(), [](const PeriodogramPeak &a, const PeriodogramPeak &b) {
        return a.period < b.period;
    });

    periods.reserve(std::min(max_peaks, sorted_peaks.size()));
    for (std::size_t i = 0; i < sorted_peaks.size() && i < max_peaks; ++i) {
        periods.push_back(sorted_peaks[i].period);
    }
    return periods;
}

std::vector<PeriodogramPeak> Periodogram::peaks(double threshold) const {
    std::vector<PeriodogramPeak> result;
    if (periods.empty() || powers.empty()) {
        return result;
    }

    const double max_power = *std::max_element(powers.begin(), powers.end());
    if (max_power <= 0.0) {
        return result;
    }

    const double cutoff = max_power * threshold;
    for (std::size_t i = 0; i < powers.size(); ++i) {
        const double power = powers[i];
        if (power < cutoff) {
            continue;
        }
        const double prev = (i > 0) ? powers[i - 1] : power;
        const double next = (i + 1 < powers.size()) ? powers[i + 1] : power;
        if (power >= prev && power >= next) {
            PeriodogramPeak peak;
            peak.power = power;
            peak.period = periods[i];
            peak.prev_period = (i > 0) ? periods[i - 1] : periods[i];
            peak.next_period = (i + 1 < periods.size()) ? periods[i + 1] : periods[i];
            result.push_back(peak);
        }
    }

    std::sort(result.begin(), result.end(), [](const PeriodogramPeak& a, const PeriodogramPeak& b) {
        return a.power > b.power;
    });
    return result;
}

} // namespace anofoxtime::seasonality
