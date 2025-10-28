#include "anofox-time/models/croston_classic.hpp"
#include "anofox-time/models/croston_optimized.hpp"
#include "anofox-time/models/croston_sba.hpp"
#include "anofox-time/models/tsb.hpp"
#include "anofox-time/models/adida.hpp"
#include "anofox-time/models/imapa.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/utils/metrics.hpp"

#include <iostream>
#include <iomanip>
#include <vector>
#include <random>
#include <chrono>
#include <algorithm>

using namespace anofoxtime;
using namespace anofoxtime::models;
using namespace anofoxtime::core;

// Helper to create TimeSeries from vector
TimeSeries createTimeSeries(const std::vector<double>& data) {
    std::vector<TimeSeries::TimePoint> timestamps;
    auto start = TimeSeries::TimePoint{};
    for (size_t i = 0; i < data.size(); ++i) {
        timestamps.push_back(start + std::chrono::seconds(static_cast<long>(i)));
    }
    return TimeSeries(timestamps, data);
}

// Generate synthetic intermittent demand data using Poisson process
std::vector<double> generateIntermittentData(int n, double lambda, unsigned int seed = 42) {
    std::mt19937 gen(seed);
    std::poisson_distribution<int> poisson(lambda);
    std::uniform_real_distribution<double> demand_dist(5.0, 15.0);
    
    std::vector<double> data;
    data.reserve(n);
    
    for (int i = 0; i < n; ++i) {
        int occurrence = poisson(gen);
        if (occurrence > 0) {
            data.push_back(demand_dist(gen));
        } else {
            data.push_back(0.0);
        }
    }
    
    return data;
}

// Compute sparsity (percentage of zeros)
double computeSparsity(const std::vector<double>& data) {
    int zeros = 0;
    for (double val : data) {
        if (val == 0.0) zeros++;
    }
    return 100.0 * zeros / data.size();
}

struct ModelResult {
    std::string name;
    double forecast;
    double mae;
    double execution_time_ms;
};

int main() {
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║        Intermittent Demand Forecasting - Example             ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";
    
    // Generate three scenarios with different sparsity levels
    struct Scenario {
        std::string name;
        double lambda;
        std::string description;
    };
    
    std::vector<Scenario> scenarios = {
        {"High Sparsity", 0.1, "~90% zeros, very sparse"},
        {"Medium Sparsity", 0.3, "~70% zeros, moderately sparse"},
        {"Low Sparsity", 0.7, "~50% zeros, less sparse"}
    };
    
    int n_train = 100;
    int n_test = 12;
    int horizon = n_test;
    
    for (const auto& scenario : scenarios) {
        std::cout << "\n" << std::string(70, '=') << "\n";
        std::cout << "Scenario: " << scenario.name << " (" << scenario.description << ")\n";
        std::cout << std::string(70, '=') << "\n\n";
        
        // Generate data
        auto full_data = generateIntermittentData(n_train + n_test, scenario.lambda);
        std::vector<double> train_data(full_data.begin(), full_data.begin() + n_train);
        std::vector<double> test_data(full_data.begin() + n_train, full_data.end());
        
        double sparsity = computeSparsity(train_data);
        std::cout << "Training size: " << n_train << "\n";
        std::cout << "Test size: " << n_test << "\n";
        std::cout << "Actual sparsity: " << std::fixed << std::setprecision(1) << sparsity << "%\n\n";
        
        auto ts_train = createTimeSeries(train_data);
        
        // Results storage
        std::vector<ModelResult> results;
        
        // Test each model
        auto run_model = [&](auto& model, const std::string& name) {
            auto start = std::chrono::high_resolution_clock::now();
            
            try {
                model.fit(ts_train);
                auto forecast = model.predict(horizon);
                
                auto end = std::chrono::high_resolution_clock::now();
                double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
                
                // Compute MAE on test set
                double mae = utils::Metrics::mae(test_data, forecast.primary());
                
                results.push_back({name, forecast.primary()[0], mae, elapsed_ms});
                
            } catch (const std::exception& e) {
                std::cout << "  ❌ " << name << " failed: " << e.what() << "\n";
            }
        };
        
        // Run all models
        CrostonClassic croston_classic;
        run_model(croston_classic, "CrostonClassic");
        
        CrostonOptimized croston_optimized;
        run_model(croston_optimized, "CrostonOptimized");
        
        CrostonSBA croston_sba;
        run_model(croston_sba, "CrostonSBA");
        
        TSB tsb(0.1, 0.1);
        run_model(tsb, "TSB(α_d=0.1, α_p=0.1)");
        
        TSB tsb_opt(0.2, 0.2);
        run_model(tsb_opt, "TSB(α_d=0.2, α_p=0.2)");
        
        ADIDA adida;
        run_model(adida, "ADIDA");
        
        IMAPA imapa;
        run_model(imapa, "IMAPA");
        
        // Sort by MAE
        std::sort(results.begin(), results.end(), 
                  [](const ModelResult& a, const ModelResult& b) {
                      return a.mae < b.mae;
                  });
        
        // Display results
        std::cout << "Results (ranked by MAE):\n";
        std::cout << std::string(70, '-') << "\n";
        std::cout << std::setw(5) << "Rank" 
                  << std::setw(25) << "Model" 
                  << std::setw(15) << "Forecast"
                  << std::setw(12) << "MAE"
                  << std::setw(13) << "Time (ms)\n";
        std::cout << std::string(70, '-') << "\n";
        
        for (size_t i = 0; i < results.size(); ++i) {
            std::cout << std::setw(5) << (i + 1)
                      << std::setw(25) << results[i].name
                      << std::setw(15) << std::fixed << std::setprecision(3) << results[i].forecast
                      << std::setw(12) << std::setprecision(3) << results[i].mae
                      << std::setw(13) << std::setprecision(2) << results[i].execution_time_ms
                      << "\n";
        }
        
        std::cout << std::string(70, '-') << "\n";
        std::cout << "🏆 Best model: " << results[0].name 
                  << " (MAE: " << std::setprecision(3) << results[0].mae << ")\n";
    }
    
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "Summary\n";
    std::cout << std::string(70, '=') << "\n\n";
    
    std::cout << "Intermittent Demand Forecasting Methods:\n\n";
    
    std::cout << "1. CrostonClassic:\n";
    std::cout << "   • Fixed α = 0.1 for demand and intervals\n";
    std::cout << "   • Formula: ŷ = ẑ / p̂\n";
    std::cout << "   • Fast, simple baseline\n\n";
    
    std::cout << "2. CrostonOptimized:\n";
    std::cout << "   • Optimizes α separately for demand and intervals\n";
    std::cout << "   • Uses Nelder-Mead with bounds [0.1, 0.3]\n";
    std::cout << "   • Better accuracy, slower\n\n";
    
    std::cout << "3. CrostonSBA:\n";
    std::cout << "   • Debiased Croston: ŷ = 0.95 * (ẑ / p̂)\n";
    std::cout << "   • Reduces forecast bias\n";
    std::cout << "   • Recommended for highly intermittent data\n\n";
    
    std::cout << "4. TSB (Teunter-Syntetos-Babai):\n";
    std::cout << "   • Uses probability instead of intervals\n";
    std::cout << "   • Formula: ŷ = d̂ * ẑ\n";
    std::cout << "   • Tunable α_d and α_p parameters\n\n";
    
    std::cout << "5. ADIDA:\n";
    std::cout << "   • Temporal aggregation approach\n";
    std::cout << "   • Reduces zeros before forecasting\n";
    std::cout << "   • Good for very sparse data\n\n";
    
    std::cout << "6. IMAPA:\n";
    std::cout << "   • Multiple aggregation levels\n";
    std::cout << "   • Averages forecasts across levels\n";
    std::cout << "   • Most sophisticated, slowest\n\n";
    
    std::cout << "Key Observations:\n";
    std::cout << "  • All methods specialize in sparse/intermittent demand\n";
    std::cout << "  • Croston family separates demand size from intervals\n";
    std::cout << "  • ADIDA/IMAPA use temporal aggregation\n";
    std::cout << "  • TSB uses probability-based approach\n";
    std::cout << "  • Optimization improves accuracy at computational cost\n\n";
    
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    std::cout << "✅ Example completed successfully!\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n\n";
    
    return 0;
}


