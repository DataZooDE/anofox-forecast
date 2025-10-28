#include "seasonality_function.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension_entries.hpp"

// anofox-time includes
#include "anofox-time/seasonality/detector.hpp"
#include "anofox-time/seasonality/analyzer.hpp"
#include "anofox-time/core/time_series.hpp"

namespace duckdb {

using namespace anofoxtime;

// Helper to extract double array from LIST
static std::vector<double> ExtractDoubleArray(Vector &vec, idx_t index, UnifiedVectorFormat &format) {
    std::vector<double> result;
    
    auto list_entry = UnifiedVectorFormat::GetData<list_entry_t>(format)[index];
    auto &child = ListVector::GetEntry(vec);
    
    UnifiedVectorFormat child_format;
    child.ToUnifiedFormat(ListVector::GetListSize(vec), child_format);
    auto child_data = UnifiedVectorFormat::GetData<double>(child_format);
    
    for (idx_t i = 0; i < list_entry.length; i++) {
        auto idx = list_entry.offset + i;
        auto mapped_idx = child_format.sel->get_index(idx);
        if (child_format.validity.RowIsValid(mapped_idx)) {
            result.push_back(child_data[mapped_idx]);
        }
    }
    
    return result;
}

// Helper to extract timestamp array from LIST
static std::vector<int64_t> ExtractTimestampArray(Vector &vec, idx_t index, UnifiedVectorFormat &format) {
    std::vector<int64_t> result;
    
    auto list_entry = UnifiedVectorFormat::GetData<list_entry_t>(format)[index];
    auto &child = ListVector::GetEntry(vec);
    
    UnifiedVectorFormat child_format;
    child.ToUnifiedFormat(ListVector::GetListSize(vec), child_format);
    auto child_data = UnifiedVectorFormat::GetData<timestamp_t>(child_format);
    
    for (idx_t i = 0; i < list_entry.length; i++) {
        auto idx = list_entry.offset + i;
        auto mapped_idx = child_format.sel->get_index(idx);
        if (child_format.validity.RowIsValid(mapped_idx)) {
            result.push_back(Timestamp::GetEpochMs(child_data[mapped_idx]));
        }
    }
    
    return result;
}

// ============================================================================
// TS_DETECT_SEASONALITY: Simple seasonality detection
// Returns list of detected seasonal periods
// ============================================================================

static void TSDetectSeasonalityFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    
    UnifiedVectorFormat values_format;
    values_vec.ToUnifiedFormat(args.size(), values_format);
    
    auto result_data = FlatVector::GetData<list_entry_t>(result);
    auto &result_validity = FlatVector::Validity(result);
    
    for (idx_t i = 0; i < args.size(); i++) {
        auto values_idx = values_format.sel->get_index(i);
        
        if (!values_format.validity.RowIsValid(values_idx)) {
            result_validity.SetInvalid(i);
            continue;
        }
        
        auto values = ExtractDoubleArray(values_vec, values_idx, values_format);
        
        if (values.size() < 8) {
            // Not enough data for seasonality detection, return empty list
            auto result_offset = ListVector::GetListSize(result);
            result_data[i].offset = result_offset;
            result_data[i].length = 0;
            continue;
        }
        
        // Detect seasonality
        std::vector<uint32_t> detected_periods;
        try {
            auto detector = seasonality::SeasonalityDetector::builder()
                .minPeriod(4)
                .threshold(0.9)
                .build();
            detected_periods = detector.detect(values, 3);
        } catch (const std::exception& e) {
            // Return empty list on error
        }
        
        // Build result list
        auto result_offset = ListVector::GetListSize(result);
        ListVector::Reserve(result, result_offset + detected_periods.size());
        
        auto &result_child = ListVector::GetEntry(result);
        auto result_child_data = FlatVector::GetData<int32_t>(result_child);
        
        for (size_t j = 0; j < detected_periods.size(); j++) {
            result_child_data[result_offset + j] = static_cast<int32_t>(detected_periods[j]);
        }
        
        result_data[i].offset = result_offset;
        result_data[i].length = detected_periods.size();
        
        ListVector::SetListSize(result, result_offset + detected_periods.size());
    }
}

// ============================================================================
// TS_ANALYZE_SEASONALITY: Detailed seasonality analysis
// Returns struct with detected periods, strengths, and primary period
// ============================================================================

static void TSAnalyzeSeasonalityFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &timestamps_vec = args.data[0];
    auto &values_vec = args.data[1];
    
    UnifiedVectorFormat timestamps_format, values_format;
    timestamps_vec.ToUnifiedFormat(args.size(), timestamps_format);
    values_vec.ToUnifiedFormat(args.size(), values_format);
    
    auto &result_validity = FlatVector::Validity(result);
    auto &child_vectors = StructVector::GetEntries(result);
    
    auto detected_periods_data = FlatVector::GetData<list_entry_t>(*child_vectors[0]);
    auto primary_period_data = FlatVector::GetData<int32_t>(*child_vectors[1]);
    auto &primary_period_validity = FlatVector::Validity(*child_vectors[1]);
    auto seasonal_strength_data = FlatVector::GetData<double>(*child_vectors[2]);
    auto trend_strength_data = FlatVector::GetData<double>(*child_vectors[3]);
    
    for (idx_t i = 0; i < args.size(); i++) {
        auto timestamps_idx = timestamps_format.sel->get_index(i);
        auto values_idx = values_format.sel->get_index(i);
        
        if (!timestamps_format.validity.RowIsValid(timestamps_idx) || 
            !values_format.validity.RowIsValid(values_idx)) {
            result_validity.SetInvalid(i);
            continue;
        }
        
        auto timestamps_ms = ExtractTimestampArray(timestamps_vec, timestamps_idx, timestamps_format);
        auto values = ExtractDoubleArray(values_vec, values_idx, values_format);
        
        if (timestamps_ms.size() != values.size()) {
            throw InvalidInputException("timestamps and values arrays must have the same length");
        }
        if (values.size() < 8) {
            result_validity.SetInvalid(i);
            continue;
        }
        
        // Convert int64 (ms) to TimePoint
        std::vector<std::chrono::system_clock::time_point> timestamps;
        timestamps.reserve(timestamps_ms.size());
        for (auto ms : timestamps_ms) {
            timestamps.push_back(std::chrono::system_clock::time_point(std::chrono::milliseconds(ms)));
        }
        
        // Build TimeSeries
        core::TimeSeries ts(timestamps, values);
        
        // Configure detector
        auto detector = seasonality::SeasonalityDetector::builder()
            .minPeriod(4)
            .threshold(0.9)
            .build();
        
        seasonality::SeasonalityAnalyzer analyzer(detector);
        
        try {
            auto analysis = analyzer.analyze(ts);
            
            // Fill struct fields
            // Field 0: detected_periods (LIST<INT>)
            auto &detected_periods_vec = *child_vectors[0];
            auto detected_offset = ListVector::GetListSize(detected_periods_vec);
            ListVector::Reserve(detected_periods_vec, detected_offset + analysis.detected_periods.size());
            auto &detected_child = ListVector::GetEntry(detected_periods_vec);
            auto detected_child_data = FlatVector::GetData<int32_t>(detected_child);
            
            for (size_t j = 0; j < analysis.detected_periods.size(); j++) {
                detected_child_data[detected_offset + j] = static_cast<int32_t>(analysis.detected_periods[j]);
            }
            
            detected_periods_data[i].offset = detected_offset;
            detected_periods_data[i].length = analysis.detected_periods.size();
            ListVector::SetListSize(detected_periods_vec, detected_offset + analysis.detected_periods.size());
            
            // Field 1: primary_period (INT)
            if (analysis.selected_period.has_value()) {
                primary_period_data[i] = static_cast<int32_t>(*analysis.selected_period);
                primary_period_validity.SetValid(i);
            } else {
                primary_period_validity.SetInvalid(i);
            }
            
            // Field 2: seasonal_strength (DOUBLE)
            seasonal_strength_data[i] = analysis.seasonal_strength;
            
            // Field 3: trend_strength (DOUBLE)
            trend_strength_data[i] = analysis.trend_strength;
            
        } catch (const std::exception& e) {
            // Return null struct on error
            result_validity.SetInvalid(i);
        }
    }
}

// ============================================================================
// Registration
// ============================================================================

void RegisterSeasonalityFunction(ExtensionLoader &loader) {
    // TS_DETECT_SEASONALITY(values: DOUBLE[]) -> INT[]
    // Simple detection returning list of seasonal periods
    ScalarFunction ts_detect_seasonality(
        "ts_detect_seasonality",
        {LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::LIST(LogicalType::INTEGER),
        TSDetectSeasonalityFunction
    );
    loader.RegisterFunction(ts_detect_seasonality);
    
    // TS_ANALYZE_SEASONALITY(timestamps: TIMESTAMP[], values: DOUBLE[])
    // -> STRUCT(detected_periods INT[], primary_period INT, seasonal_strength DOUBLE, trend_strength DOUBLE)
    child_list_t<LogicalType> struct_children;
    struct_children.push_back({"detected_periods", LogicalType::LIST(LogicalType::INTEGER)});
    struct_children.push_back({"primary_period", LogicalType::INTEGER});
    struct_children.push_back({"seasonal_strength", LogicalType::DOUBLE});
    struct_children.push_back({"trend_strength", LogicalType::DOUBLE});
    
    ScalarFunction ts_analyze_seasonality(
        "ts_analyze_seasonality",
        {LogicalType::LIST(LogicalType::TIMESTAMP), LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::STRUCT(struct_children),
        TSAnalyzeSeasonalityFunction
    );
    loader.RegisterFunction(ts_analyze_seasonality);
}

} // namespace duckdb
