#pragma once

#include "anofox-time/core/time_series.hpp"
#include "anofox-time/seasonality/stl.hpp"
#include <optional>
#include <vector>

namespace anofoxtime::seasonality {

struct MSTLComponents {
    std::vector<double> trend;
    std::vector<std::vector<double>> seasonal;
    std::vector<double> remainder;
};

class MSTLDecomposition {
public:
    class Builder {
    public:
        Builder& withPeriods(std::vector<std::size_t> periods);
        Builder& withIterations(std::size_t iterations);
        Builder& withRobust(bool robust);
        MSTLDecomposition build() const;

    private:
        std::vector<std::size_t> periods_ = {7};
        std::size_t iterations_ = 2;
        bool robust_ = false;
    };

    static Builder builder();

    MSTLDecomposition(std::vector<std::size_t> periods,
                      std::size_t iterations,
                      bool robust);

    void fit(const core::TimeSeries& ts);

    const MSTLComponents& components() const { return components_; }

private:
    std::vector<std::size_t> periods_;
    std::size_t iterations_;
    bool robust_;
    MSTLComponents components_;
};

} // namespace anofoxtime::seasonality
