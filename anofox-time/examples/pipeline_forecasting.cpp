#include "anofox-time/quick.hpp"
#include "anofox-time/transform/transformers.hpp"
#include "anofox-time/utils/metrics.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <optional>
#include <random>
#include <string>
#include <type_traits>
#include <vector>

using namespace anofoxtime;

namespace {

std::vector<double> generateSeasonalDemand(std::size_t length) {
	std::mt19937 rng(42);
	std::normal_distribution<double> noise(0.0, 2.5);

	std::vector<double> data;
	data.reserve(length);

	for (std::size_t i = 0; i < length; ++i) {
		const double seasonal = 10.0 * std::sin(2.0 * M_PI * static_cast<double>(i % 12) / 12.0);
		const double trend = 0.3 * static_cast<double>(i);
		const double promo = (i % 36 == 0) ? 25.0 : 0.0;
		data.push_back(120.0 + trend + seasonal + promo + noise(rng));
	}

	return data;
}

struct Scenario {
	std::vector<double> history;
	std::vector<double> actual;
};

Scenario buildScenario(std::size_t history_points, int horizon) {
	const auto full = generateSeasonalDemand(history_points + static_cast<std::size_t>(horizon));
	Scenario scenario;
	scenario.history.assign(full.begin(), full.begin() + history_points);
	scenario.actual.assign(full.begin() + history_points, full.end());
	return scenario;
}

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

void summarizeCandidate(const quick::AutoSelectCandidateSummary &candidate, std::size_t rank) {
	std::cout << "  [" << rank << "] " << candidate.name;
	if (std::isfinite(candidate.score)) {
		std::cout << "  (score = " << std::fixed << std::setprecision(4) << candidate.score << ")";
	}
	std::cout << '\n';
	std::cout.unsetf(std::ios::floatfield);

	if (candidate.forecast.metrics.has_value()) {
		printMetrics(*candidate.forecast.metrics);
	}

	if (candidate.backtest.has_value()) {
		std::cout << "    Backtest folds: " << candidate.backtest->folds.size() << '\n';
		printMetrics(candidate.backtest->aggregate);
	}
}

} // namespace

int main() {
	const int horizon = 12;
	const Scenario scenario = buildScenario(180, horizon);
	const std::vector<double> baseline(horizon, scenario.history.back());

	quick::AutoSelectOptions options;
	options.horizon = horizon;
	options.include_backtest = true;
	options.backtest_config.horizon = horizon;
	options.backtest_config.min_train = 84;
	options.backtest_config.step = horizon / 2;
	options.backtest_config.max_folds = 4;

	options.sma_windows = {6, 12};
	options.ses_alphas = {0.2, 0.4};
	options.holt_params = {{0.2, 0.1}, {0.35, 0.15}};

	quick::ETSOptions ets_add;
	ets_add.trend = models::ETSTrendType::Additive;
	ets_add.season = models::ETSSeasonType::Additive;
	ets_add.season_length = 12;
	ets_add.alpha = 0.3;
	ets_add.beta = 0.15;
	ets_add.gamma = 0.1;
	options.ets_configs.push_back(ets_add);

	options.actual = scenario.actual;
	options.baseline = baseline;
	options.pipeline_factory = [] {
		std::vector<std::unique_ptr<transform::Transformer>> transforms;

		auto scaler = std::make_unique<transform::StandardScaler>();
		scaler->ignoreNaNs(true);
		transforms.emplace_back(std::move(scaler));

		auto yeo = std::make_unique<transform::YeoJohnson>();
		yeo->ignoreNaNs(true);
		transforms.emplace_back(std::move(yeo));

		return std::make_unique<transform::Pipeline>(std::move(transforms));
	};

	const auto result = quick::autoSelect(scenario.history, options);

	std::cout << "=== Pipeline Forecasting Scenario ===\n";
	std::cout << "Best model: " << result.model_name << '\n';
	if (result.forecast.metrics.has_value()) {
		printMetrics(*result.forecast.metrics);
	}

	if (!result.forecast.forecast.primary().empty()) {
		std::cout << "  Forecast (first 5 points): ";
		const auto &pred = result.forecast.forecast.primary();
		for (std::size_t i = 0; i < std::min<std::size_t>(5, pred.size()); ++i) {
			std::cout << std::fixed << std::setprecision(2) << pred[i] << (i + 1 < std::min<std::size_t>(5, pred.size()) ? ", " : "\n");
		}
		std::cout.unsetf(std::ios::floatfield);
	}

	if (!result.candidates.empty()) {
		std::vector<const quick::AutoSelectCandidateSummary *> ranked;
		ranked.reserve(result.candidates.size());
		for (const auto &candidate : result.candidates) {
			ranked.push_back(&candidate);
		}
		std::sort(ranked.begin(), ranked.end(), [](const auto *lhs, const auto *rhs) {
			const double l = std::isfinite(lhs->score) ? lhs->score : std::numeric_limits<double>::infinity();
			const double r = std::isfinite(rhs->score) ? rhs->score : std::numeric_limits<double>::infinity();
			return l < r;
		});

		std::cout << "\nTop candidates\n";
		for (std::size_t i = 0; i < ranked.size() && i < 3; ++i) {
			summarizeCandidate(*ranked[i], i + 1);
		}
	}

	if (!result.failures.empty()) {
		std::cout << "\nSkipped candidates\n";
		for (const auto &failure : result.failures) {
			std::cout << "  " << failure.first << ": " << failure.second << '\n';
		}
	}

	return 0;
}
