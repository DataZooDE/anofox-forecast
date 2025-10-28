#pragma once

#include "anofox-time/validation.hpp"

#include <cmath>
#include <cstddef>
#include <random>
#include <vector>

namespace tests::fixtures {

inline std::vector<double> monitoringSignal(std::size_t length = 384) {
	std::mt19937 rng(7);
	std::normal_distribution<double> noise(0.0, 1.2);
	constexpr double pi = 3.14159265358979323846;

	std::vector<double> data;
	data.reserve(length);

	for (std::size_t i = 0; i < length; ++i) {
		const double daily = 5.0 * std::sin(2.0 * pi * static_cast<double>(i % 24) / 24.0);
		const double weekly = 8.0 * std::sin(2.0 * pi * static_cast<double>(i % 168) / 168.0);
		double value = 75.0 + daily + weekly + noise(rng);

		if (i == 96 || i == 192) {
			value += 25.0;
		}
		if (i > 240 && i < 300) {
			value -= 15.0;
		}
		if (i == 360) {
			value -= 35.0;
		}

		data.push_back(value);
	}

	return data;
}

inline const std::vector<std::size_t> &monitoringPointAnomalies() {
	static const std::vector<std::size_t> indices{192, 283, 360};
	return indices;
}

inline const std::vector<std::size_t> &monitoringChangepoints() {
	static const std::vector<std::size_t> indices{0, 247, 300, 310, 383};
	return indices;
}

inline const std::vector<std::size_t> &monitoringSegmentOutliers() {
	static const std::vector<std::size_t> indices{0, 1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 12, 14};
	return indices;
}

inline std::vector<std::vector<double>> monitoringWindows(const std::vector<double> &series,
                                                          std::size_t window = 48,
                                                          std::size_t step = 24) {
	std::vector<std::vector<double>> windows;
	if (series.size() < window || step == 0) {
		return windows;
	}
	for (std::size_t start = 0; start + window <= series.size(); start += step) {
		const auto begin = series.begin() + static_cast<std::ptrdiff_t>(start);
		const auto end = begin + static_cast<std::ptrdiff_t>(window);
		windows.emplace_back(begin, end);
	}
	return windows;
}

inline anofoxtime::validation::RollingCVConfig monitoringCVConfig() {
	anofoxtime::validation::RollingCVConfig cfg;
	cfg.horizon = 24;
	cfg.min_train = 96;
	cfg.step = 24;
	cfg.max_folds = 6;
	return cfg;
}

} // namespace tests::fixtures

