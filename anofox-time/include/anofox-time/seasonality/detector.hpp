#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace anofoxtime::seasonality {

struct PeriodogramPeak {
    std::uint32_t period = 0;
    double power = 0.0;
    std::uint32_t prev_period = 0;
    std::uint32_t next_period = 0;
};

struct Periodogram {
    std::vector<std::uint32_t> periods;
    std::vector<double> powers;

    [[nodiscard]] std::vector<PeriodogramPeak> peaks(double threshold) const;
};

class SeasonalityDetector {
public:
    class Builder {
    public:
        Builder& minPeriod(std::uint32_t value);
        Builder& maxPeriod(std::uint32_t value);
        Builder& threshold(double value);
        SeasonalityDetector build() const;

    private:
        std::uint32_t min_period_ = 4;
        std::optional<std::uint32_t> max_period_;
        double threshold_ = 0.9;
    };

    static Builder builder();

    Periodogram periodogram(const std::vector<double>& data) const;
    std::vector<std::uint32_t> detect(const std::vector<double>& data,
                                      std::size_t max_peaks = 3) const;

private:
    SeasonalityDetector(std::uint32_t min_period,
                        std::optional<std::uint32_t> max_period,
                        double threshold);

    std::uint32_t min_period_;
    std::optional<std::uint32_t> max_period_;
    double threshold_;
};

} // namespace anofoxtime::seasonality
