#include "anofox-time/outlier/dbscan_outlier.hpp"
#include "anofox-time/quick.hpp"
#include "anofox-time/utils/metrics.hpp"
#include "anofox-time/validation.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <optional>
#include <random>
#include <type_traits>
#include <vector>

using namespace anofoxtime;

namespace {

std::vector<double> synthesizeSignal(std::size_t length) {
	std::mt19937 rng(7);
	std::normal_distribution<double> noise(0.0, 1.2);

	std::vector<double> data;
	data.reserve(length);

	for (std::size_t i = 0; i < length; ++i) {
		const double daily = 5.0 * std::sin(2.0 * M_PI * static_cast<double>(i % 24) / 24.0);
		const double weekly = 8.0 * std::sin(2.0 * M_PI * static_cast<double>(i % 168) / 168.0);
		double value = 75.0 + daily + weekly + noise(rng);

		if (i == 96 || i == 192) {
			value += 25.0; // sudden surge
		}
		if (i > 240 && i < 300) {
			value -= 15.0; // sustained drop
		}
		if (i == 360) {
			value -= 35.0; // sharp outage
		}

		data.push_back(value);
	}

	return data;
}

std::vector<std::vector<double>> slidingWindows(const std::vector<double> &series, std::size_t window) {
	std::vector<std::vector<double>> windows;
	if (series.size() < window) {
		return windows;
	}
	for (std::size_t start = 0; start + window <= series.size(); start += window / 2) {
		windows.emplace_back(series.begin() + static_cast<std::ptrdiff_t>(start),
		                     series.begin() + static_cast<std::ptrdiff_t>(start + window));
	}
	return windows;
}

void printIndices(const std::string &label, const std::vector<std::size_t> &indices) {
	std::cout << label;
	if (indices.empty()) {
		std::cout << " none\n";
		return;
	}
	std::cout << ' ';
	for (std::size_t i = 0; i < indices.size(); ++i) {
		std::cout << indices[i];
		if (i + 1 < indices.size()) {
			std::cout << ", ";
		}
	}
	std::cout << '\n';
}

const validation::RollingBacktestFold *worstFold(const validation::RollingBacktestSummary &summary) {
	if (summary.folds.empty()) {
		return nullptr;
	}

	const auto it = std::max_element(
	    summary.folds.begin(),
	    summary.folds.end(),
	    [](const validation::RollingBacktestFold &lhs, const validation::RollingBacktestFold &rhs) {
		    return lhs.metrics.mae < rhs.metrics.mae;
	    });
	return it != summary.folds.end() ? &*it : nullptr;
}

} // namespace

void printMetrics(const utils::AccuracyMetrics &metrics) {
	auto print = [](const char *label, const auto &value) {
		std::optional<double> maybe_value;

		if constexpr (std::is_same_v<std::decay_t<decltype(value)>, std::optional<double>>) {
			if (value && std::isfinite(*value)) {
				maybe_value = *value;
			}
		} else if (std::isfinite(value)) {
			maybe_value = value;
		}

		if (maybe_value && std::isfinite(*maybe_value)) {
			std::cout << "    " << std::setw(8) << label << ": " << std::fixed << std::setprecision(4) << *maybe_value << '\n';
		}
	};

	std::cout << "  Accuracy metrics\n";
	print("MAE", metrics.mae);
	print("RMSE", metrics.rmse);
	print("sMAPE", metrics.smape);
	print("MASE", metrics.mase);
	print("R^2", metrics.r_squared);
	std::cout.unsetf(std::ios::floatfield);
}

int main() {
	const auto signal = synthesizeSignal(384);

	std::cout << "=== Monitoring & Diagnostics Scenario ===\n";

	const auto outliers = quick::detectOutliersMAD(signal, 3.0);
	printIndices("Point anomalies:", outliers.outlier_indices);

	const auto changepoints = quick::detectChangepoints(signal, 180.0);
	printIndices("Changepoints:", changepoints);

	const auto windows = slidingWindows(signal, 48);
	const auto segment_outliers = quick::detectOutliersDBSCAN(windows, 12.0, 2);
	printIndices("Segment outliers (DBSCAN):", segment_outliers.outlying_series);

	validation::RollingCVConfig cv;
	cv.horizon = 24;
	cv.min_train = 96;
	cv.step = 24;
	cv.max_folds = 6;

	const auto backtest = quick::rollingBacktestARIMA(signal, cv, 1, 1, 1, true);

	std::cout << "\nRolling backtest diagnostics\n";
	printMetrics(backtest.aggregate);

	if (const auto *fold = worstFold(backtest)) {
		std::cout << "  Worst fold at index " << fold->index
		          << " (train=" << fold->train_size << ", test=" << fold->test_size << ")\n";
		printMetrics(fold->metrics);
	}

	return 0;
}
