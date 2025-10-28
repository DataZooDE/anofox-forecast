/**
 * @file ensemble_example.cpp
 * @brief Demonstrates ensemble forecasting methods
 * 
 * This example shows how to:
 * 1. Create an ensemble of multiple forecasting models
 * 2. Use different combination methods (mean, median, weighted)
 * 3. Evaluate ensemble performance on backtesting
 * 4. Compare ensemble with individual models
 */

#include "anofox-time/models/ensemble.hpp"
#include "anofox-time/models/naive.hpp"
#include "anofox-time/models/ses.hpp"
#include "anofox-time/models/sma.hpp"
#include "anofox-time/models/theta.hpp"
#include "anofox-time/models/arima.hpp"
#include "anofox-time/models/ets.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/utils/logging.hpp"
#include "anofox-time/validation.hpp"
#include <spdlog/spdlog.h>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <vector>
#include <memory>

using namespace anofoxtime;
using namespace std::chrono_literals;

// Helper function to create example data (monthly sales with trend and seasonality)
core::TimeSeries createSalesData() {
	std::vector<core::TimeSeries::TimePoint> timestamps;
	std::vector<double> values;
	
	auto base_time = std::chrono::system_clock::now();
	
	// 3 years of monthly data
	for (int i = 0; i < 36; ++i) {
		timestamps.push_back(base_time + std::chrono::hours(24 * 30 * i));
		
		// Trend: growing business
		double trend = 1000.0 + 50.0 * i;
		
		// Annual seasonality (Q4 peak)
		double seasonal = 200.0 * std::sin(2.0 * M_PI * i / 12.0);
		
		// Some random noise
		double noise = ((i * 7) % 20) - 10.0;
		
		values.push_back(trend + seasonal + noise);
	}
	
	return core::TimeSeries(std::move(timestamps), std::move(values));
}

void printSeparator(const std::string& title = "") {
	std::cout << "\n";
	std::cout << std::string(80, '=') << "\n";
	if (!title.empty()) {
		std::cout << title << "\n";
		std::cout << std::string(80, '=') << "\n";
	}
}

void printForecast(const std::string& name, const core::Forecast& forecast, int max_print = 12) {
	std::cout << "\n" << name << " Forecast:\n";
	std::cout << std::string(50, '-') << "\n";
	
	const int horizon = std::min(max_print, static_cast<int>(forecast.horizon()));
	for (int h = 0; h < horizon; ++h) {
		std::cout << "  Month " << std::setw(2) << (h + 1) << ": " 
		         << std::fixed << std::setprecision(2) << forecast.primary()[h] << "\n";
	}
	if (forecast.horizon() > static_cast<std::size_t>(max_print)) {
		std::cout << "  ... (showing first " << max_print << " of " 
		         << forecast.horizon() << " values)\n";
	}
}

void printWeights(const models::Ensemble& ensemble) {
	const auto& forecasters = ensemble.getForecasters();
	const auto& weights = ensemble.getWeights();
	
	std::cout << "\nModel Weights:\n";
	std::cout << std::string(50, '-') << "\n";
	
	for (std::size_t i = 0; i < forecasters.size(); ++i) {
		std::cout << "  " << std::left << std::setw(20) << forecasters[i]->getName() 
		         << ": " << std::fixed << std::setprecision(4) << weights[i] << "\n";
	}
}

void printMetrics(const std::string& name, const utils::AccuracyMetrics& metrics) {
	std::cout << "\n" << name << " Metrics:\n";
	std::cout << std::string(50, '-') << "\n";
	std::cout << "  MAE:  " << std::fixed << std::setprecision(2) << metrics.mae << "\n";
	std::cout << "  RMSE: " << std::fixed << std::setprecision(2) << metrics.rmse << "\n";
	if (metrics.mape.has_value()) {
		std::cout << "  MAPE: " << std::fixed << std::setprecision(2) 
		         << *metrics.mape << "%\n";
	}
}

int main() {
	try {
		// Set logging level
#ifndef ANOFOX_NO_LOGGING
		utils::Logging::init(spdlog::level::info);
#endif
		
		printSeparator("Ensemble Forecasting Example");
		std::cout << "This example demonstrates various ensemble methods\n";
		
		// Create example data
		auto ts = createSalesData();
		std::cout << "\nData: " << ts.size() << " months of sales data\n";
		std::cout << "First value: " << std::fixed << std::setprecision(2) 
		         << ts.getValues().front() << "\n";
		std::cout << "Last value:  " << std::fixed << std::setprecision(2) 
		         << ts.getValues().back() << "\n";
		
		// Forecast horizon
		const int horizon = 12; // 1 year ahead
		
	// =================================================================
	// Example 1: Simple Mean Ensemble
	// =================================================================
	printSeparator("Example 1: Mean Ensemble");
	std::cout << "Combining forecasts using simple averaging\n";
	
	{
		std::vector<std::shared_ptr<models::IForecaster>> forecasters;
		forecasters.push_back(std::make_shared<models::Naive>());
		forecasters.push_back(models::SimpleExponentialSmoothingBuilder().withAlpha(0.3).build());
		forecasters.push_back(models::SimpleMovingAverageBuilder().withWindow(3).build());
		forecasters.push_back(std::make_shared<models::Theta>());
			
			models::EnsembleConfig config;
			config.method = models::EnsembleCombinationMethod::Mean;
			
			models::Ensemble ensemble(forecasters, config);
			ensemble.fit(ts);
			
			auto forecast = ensemble.predict(horizon);
			
			printWeights(ensemble);
			printForecast("Mean Ensemble", forecast);
			
			// Show individual forecasts for comparison
			std::cout << "\nIndividual Model Forecasts (first 6 months):\n";
			std::cout << std::string(50, '-') << "\n";
			auto individual = ensemble.getIndividualForecasts(horizon);
			for (std::size_t i = 0; i < individual.size(); ++i) {
				std::cout << "  " << std::left << std::setw(15) 
				         << forecasters[i]->getName() << ": ";
				for (int h = 0; h < std::min(6, horizon); ++h) {
					std::cout << std::fixed << std::setprecision(0) 
					         << individual[i].primary()[h] << " ";
				}
				std::cout << "...\n";
			}
		}
		
		// =================================================================
		// Example 2: Median Ensemble
		// =================================================================
		printSeparator("Example 2: Median Ensemble");
		std::cout << "Using median for robust combination (less sensitive to outliers)\n";
		
		{
			std::vector<std::shared_ptr<models::IForecaster>> forecasters;
			forecasters.push_back(std::make_shared<models::Naive>());
			forecasters.push_back(models::SimpleExponentialSmoothingBuilder().withAlpha(0.3).build());
			forecasters.push_back(models::SimpleMovingAverageBuilder().withWindow(3).build());
			forecasters.push_back(models::SimpleMovingAverageBuilder().withWindow(6).build());
			forecasters.push_back(std::make_shared<models::Theta>());
			
			models::EnsembleConfig config;
			config.method = models::EnsembleCombinationMethod::Median;
			
			models::Ensemble ensemble(forecasters, config);
			ensemble.fit(ts);
			
			auto forecast = ensemble.predict(horizon);
			printForecast("Median Ensemble", forecast);
		}
		
		// =================================================================
		// Example 3: AIC-Weighted Ensemble
		// =================================================================
		printSeparator("Example 3: AIC-Weighted Ensemble");
		std::cout << "Weighting models based on Akaike Information Criterion\n";
		std::cout << "Note: Only models with AIC will be included\n";
		
		{
			std::vector<std::shared_ptr<models::IForecaster>> forecasters;
			
			// Add ARIMA models (they have AIC)
			auto arima1 = models::ARIMABuilder()
				.withAR(1)
				.withMA(0)
				.build();
			forecasters.push_back(std::move(arima1));
			
			auto arima2 = models::ARIMABuilder()
				.withAR(2)
				.withMA(1)
				.build();
			forecasters.push_back(std::move(arima2));
			
			auto arima3 = models::ARIMABuilder()
				.withAR(1)
				.withMA(1)
				.build();
			forecasters.push_back(std::move(arima3));
			
			models::EnsembleConfig config;
			config.method = models::EnsembleCombinationMethod::WeightedAIC;
			config.temperature = 1.0; // Controls weight concentration
			
			models::Ensemble ensemble(forecasters, config);
			ensemble.fit(ts);
			
			auto forecast = ensemble.predict(horizon);
			
			printWeights(ensemble);
			printForecast("AIC-Weighted Ensemble", forecast);
			
			std::cout << "\nNote: Models with lower AIC receive higher weights\n";
		}
		
		// =================================================================
		// Example 4: Accuracy-Weighted Ensemble
		// =================================================================
		printSeparator("Example 4: Accuracy-Weighted Ensemble");
		std::cout << "Weighting models based on validation set performance\n";
		
		{
			std::vector<std::shared_ptr<models::IForecaster>> forecasters;
			forecasters.push_back(std::make_shared<models::Naive>());
			forecasters.push_back(models::SimpleExponentialSmoothingBuilder().withAlpha(0.3).build());
			forecasters.push_back(models::SimpleMovingAverageBuilder().withWindow(3).build());
			forecasters.push_back(std::make_shared<models::Theta>());
			
			models::EnsembleConfig config;
			config.method = models::EnsembleCombinationMethod::WeightedAccuracy;
			config.accuracy_metric = models::AccuracyMetric::MAE;
			config.validation_split = 0.2; // Use last 20% for validation
			config.temperature = 1.0;
			
			models::Ensemble ensemble(forecasters, config);
			ensemble.fit(ts);
			
			auto forecast = ensemble.predict(horizon);
			
			printWeights(ensemble);
			printForecast("Accuracy-Weighted Ensemble", forecast);
			
			std::cout << "\nNote: Models with better validation MAE receive higher weights\n";
		}
		
		// =================================================================
		// Example 5: Using Factories for Backtesting
		// =================================================================
		printSeparator("Example 5: Ensemble in Backtesting");
		std::cout << "Evaluating ensemble performance using rolling cross-validation\n";
		
		{
			// Define factories for fresh model instances
			std::vector<std::function<std::shared_ptr<models::IForecaster>()>> factories;
			factories.push_back([]() { return std::make_shared<models::Naive>(); });
			factories.push_back([]() { return models::SimpleExponentialSmoothingBuilder().withAlpha(0.3).build(); });
			factories.push_back([]() { return models::SimpleMovingAverageBuilder().withWindow(3).build(); });
			factories.push_back([]() { return std::make_shared<models::Theta>(); });
			
			models::EnsembleConfig config;
			config.method = models::EnsembleCombinationMethod::Mean;
			
		// Create a factory for the ensemble
		auto ensemble_factory = [factories, config]() -> std::unique_ptr<models::IForecaster> {
			return std::make_unique<models::Ensemble>(factories, config);
		};
			
			// Configure backtesting
			validation::RollingCVConfig cv_config;
			cv_config.min_train = 24; // Minimum 2 years training
			cv_config.horizon = 6;    // 6 months ahead forecast
			cv_config.step = 3;       // Move 3 months at a time
			cv_config.max_folds = 3;  // Maximum 3 folds
			cv_config.expanding = true; // Use expanding window
			
			std::cout << "\nBacktest Configuration:\n";
			std::cout << "  Min train: " << cv_config.min_train << " months\n";
			std::cout << "  Horizon: " << cv_config.horizon << " months\n";
			std::cout << "  Step: " << cv_config.step << " months\n";
			
			// Run backtest
			auto summary = validation::rollingBacktest(ts, cv_config, ensemble_factory);
			
			std::cout << "\nBacktest Results:\n";
			std::cout << std::string(50, '-') << "\n";
			std::cout << "  Number of folds: " << summary.folds.size() << "\n";
			
			printMetrics("Aggregate Performance", summary.aggregate);
			
			std::cout << "\nPer-Fold Performance:\n";
			std::cout << std::string(50, '-') << "\n";
			for (std::size_t i = 0; i < summary.folds.size(); ++i) {
				const auto& fold = summary.folds[i];
				std::cout << "  Fold " << (i + 1) << " - MAE: " 
				         << std::fixed << std::setprecision(2) << fold.metrics.mae 
				         << ", RMSE: " << fold.metrics.rmse << "\n";
			}
		}
		
		// =================================================================
		// Example 6: Comparing Ensemble with Individual Models
		// =================================================================
		printSeparator("Example 6: Ensemble vs Individual Models");
		std::cout << "Comparing ensemble performance against base models\n";
		
		{
			// Create a validation split
			const std::size_t train_size = static_cast<std::size_t>(ts.size() * 0.8);
			const std::size_t test_size = ts.size() - train_size;
			
			auto train = ts.slice(0, train_size);
			auto test = ts.slice(train_size, ts.size());
			const auto& actual = test.getValues();
			
			std::cout << "\nSplit: " << train_size << " train, " 
			         << test_size << " test\n";
			
			// Create models
			std::vector<std::shared_ptr<models::IForecaster>> all_models;
			all_models.push_back(std::make_shared<models::Naive>());
			all_models.push_back(models::SimpleExponentialSmoothingBuilder().withAlpha(0.3).build());
			all_models.push_back(models::SimpleMovingAverageBuilder().withWindow(3).build());
			all_models.push_back(std::make_shared<models::Theta>());
			
			// Individual model performance
			std::cout << "\nIndividual Model Performance:\n";
			std::cout << std::string(50, '-') << "\n";
			
			std::vector<double> individual_maes;
			for (auto& model : all_models) {
				model->fit(train);
				auto forecast = model->predict(static_cast<int>(test_size));
				auto metrics = model->score(actual, forecast.primary());
				
				std::cout << "  " << std::left << std::setw(15) << model->getName() 
				         << " - MAE: " << std::fixed << std::setprecision(2) 
				         << metrics.mae << "\n";
				individual_maes.push_back(metrics.mae);
			}
			
		// Ensemble performance
		std::vector<std::shared_ptr<models::IForecaster>> ensemble_models;
		ensemble_models.push_back(std::make_shared<models::Naive>());
		ensemble_models.push_back(models::SimpleExponentialSmoothingBuilder().withAlpha(0.3).build());
		ensemble_models.push_back(models::SimpleMovingAverageBuilder().withWindow(3).build());
		ensemble_models.push_back(std::make_shared<models::Theta>());
			
			models::EnsembleConfig config;
			config.method = models::EnsembleCombinationMethod::Mean;
			
			models::Ensemble ensemble(ensemble_models, config);
			ensemble.fit(train);
			
			auto ensemble_forecast = ensemble.predict(static_cast<int>(test_size));
			auto ensemble_metrics = ensemble.score(actual, ensemble_forecast.primary());
			
			std::cout << "\nEnsemble Performance:\n";
			std::cout << std::string(50, '-') << "\n";
			std::cout << "  " << std::left << std::setw(15) << "Mean Ensemble" 
			         << " - MAE: " << std::fixed << std::setprecision(2) 
			         << ensemble_metrics.mae << "\n";
			
			// Calculate average individual MAE
			double avg_mae = 0.0;
			for (const auto& mae : individual_maes) {
				avg_mae += mae;
			}
			avg_mae /= individual_maes.size();
			
			std::cout << "\nSummary:\n";
			std::cout << std::string(50, '-') << "\n";
			std::cout << "  Average individual MAE: " << std::fixed << std::setprecision(2) 
			         << avg_mae << "\n";
			std::cout << "  Ensemble MAE: " << ensemble_metrics.mae << "\n";
			std::cout << "  Improvement: " << std::fixed << std::setprecision(1)
			         << ((avg_mae - ensemble_metrics.mae) / avg_mae * 100.0) << "%\n";
		}
		
		printSeparator("Example Complete");
		std::cout << "\nKey Takeaways:\n";
		std::cout << "1. Mean ensemble provides robust baseline combination\n";
		std::cout << "2. Median ensemble is resistant to outlier predictions\n";
		std::cout << "3. AIC/BIC weighting leverages model selection criteria\n";
		std::cout << "4. Accuracy weighting adapts to validation performance\n";
		std::cout << "5. Ensembles often outperform individual models\n";
		std::cout << "6. Ensembles work seamlessly with backtesting framework\n";
		
	} catch (const std::exception& e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return 1;
	}
	
	return 0;
}

