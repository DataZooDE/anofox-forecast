#include "metrics_function.hpp"
#include "anofox_time_wrapper.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension_entries.hpp"

// Include anofox-time metrics
#include "anofox-time/utils/metrics.hpp"

namespace duckdb {

// Helper function to extract vector<double> from LIST
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

// TS_MAE(actual DOUBLE[], predicted DOUBLE[]) -> DOUBLE
static void TSMAEFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &actual_vec = args.data[0];
    auto &predicted_vec = args.data[1];
    
    UnifiedVectorFormat actual_format, predicted_format;
    actual_vec.ToUnifiedFormat(args.size(), actual_format);
    predicted_vec.ToUnifiedFormat(args.size(), predicted_format);
    
    auto result_data = FlatVector::GetData<double>(result);
    auto &result_validity = FlatVector::Validity(result);
    
    for (idx_t i = 0; i < args.size(); i++) {
        auto actual_idx = actual_format.sel->get_index(i);
        auto predicted_idx = predicted_format.sel->get_index(i);
        
        if (!actual_format.validity.RowIsValid(actual_idx) || 
            !predicted_format.validity.RowIsValid(predicted_idx)) {
            result_validity.SetInvalid(i);
            continue;
        }
        
        auto actual = ExtractDoubleArray(actual_vec, actual_idx, actual_format);
        auto predicted = ExtractDoubleArray(predicted_vec, predicted_idx, predicted_format);
        
        if (actual.size() != predicted.size()) {
            throw InvalidInputException("actual and predicted arrays must have the same length");
        }
        if (actual.empty()) {
            throw InvalidInputException("arrays must not be empty");
        }
        
        result_data[i] = ::anofoxtime::utils::Metrics::mae(actual, predicted);
    }
}

// TS_MSE(actual DOUBLE[], predicted DOUBLE[]) -> DOUBLE
static void TSMSEFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &actual_vec = args.data[0];
    auto &predicted_vec = args.data[1];
    
    UnifiedVectorFormat actual_format, predicted_format;
    actual_vec.ToUnifiedFormat(args.size(), actual_format);
    predicted_vec.ToUnifiedFormat(args.size(), predicted_format);
    
    auto result_data = FlatVector::GetData<double>(result);
    auto &result_validity = FlatVector::Validity(result);
    
    for (idx_t i = 0; i < args.size(); i++) {
        auto actual_idx = actual_format.sel->get_index(i);
        auto predicted_idx = predicted_format.sel->get_index(i);
        
        if (!actual_format.validity.RowIsValid(actual_idx) || 
            !predicted_format.validity.RowIsValid(predicted_idx)) {
            result_validity.SetInvalid(i);
            continue;
        }
        
        auto actual = ExtractDoubleArray(actual_vec, actual_idx, actual_format);
        auto predicted = ExtractDoubleArray(predicted_vec, predicted_idx, predicted_format);
        
        if (actual.size() != predicted.size()) {
            throw InvalidInputException("actual and predicted arrays must have the same length");
        }
        if (actual.empty()) {
            throw InvalidInputException("arrays must not be empty");
        }
        
        result_data[i] = ::anofoxtime::utils::Metrics::mse(actual, predicted);
    }
}

// TS_RMSE(actual DOUBLE[], predicted DOUBLE[]) -> DOUBLE
static void TSRMSEFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &actual_vec = args.data[0];
    auto &predicted_vec = args.data[1];
    
    UnifiedVectorFormat actual_format, predicted_format;
    actual_vec.ToUnifiedFormat(args.size(), actual_format);
    predicted_vec.ToUnifiedFormat(args.size(), predicted_format);
    
    auto result_data = FlatVector::GetData<double>(result);
    auto &result_validity = FlatVector::Validity(result);
    
    for (idx_t i = 0; i < args.size(); i++) {
        auto actual_idx = actual_format.sel->get_index(i);
        auto predicted_idx = predicted_format.sel->get_index(i);
        
        if (!actual_format.validity.RowIsValid(actual_idx) || 
            !predicted_format.validity.RowIsValid(predicted_idx)) {
            result_validity.SetInvalid(i);
            continue;
        }
        
        auto actual = ExtractDoubleArray(actual_vec, actual_idx, actual_format);
        auto predicted = ExtractDoubleArray(predicted_vec, predicted_idx, predicted_format);
        
        if (actual.size() != predicted.size()) {
            throw InvalidInputException("actual and predicted arrays must have the same length");
        }
        if (actual.empty()) {
            throw InvalidInputException("arrays must not be empty");
        }
        
        result_data[i] = ::anofoxtime::utils::Metrics::rmse(actual, predicted);
    }
}

// TS_MAPE(actual DOUBLE[], predicted DOUBLE[]) -> DOUBLE
static void TSMAPEFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &actual_vec = args.data[0];
    auto &predicted_vec = args.data[1];
    
    UnifiedVectorFormat actual_format, predicted_format;
    actual_vec.ToUnifiedFormat(args.size(), actual_format);
    predicted_vec.ToUnifiedFormat(args.size(), predicted_format);
    
    auto result_data = FlatVector::GetData<double>(result);
    auto &result_validity = FlatVector::Validity(result);
    
    for (idx_t i = 0; i < args.size(); i++) {
        auto actual_idx = actual_format.sel->get_index(i);
        auto predicted_idx = predicted_format.sel->get_index(i);
        
        if (!actual_format.validity.RowIsValid(actual_idx) || 
            !predicted_format.validity.RowIsValid(predicted_idx)) {
            result_validity.SetInvalid(i);
            continue;
        }
        
        auto actual = ExtractDoubleArray(actual_vec, actual_idx, actual_format);
        auto predicted = ExtractDoubleArray(predicted_vec, predicted_idx, predicted_format);
        
        if (actual.size() != predicted.size()) {
            throw InvalidInputException("actual and predicted arrays must have the same length");
        }
        if (actual.empty()) {
            throw InvalidInputException("arrays must not be empty");
        }
        
        auto mape = ::anofoxtime::utils::Metrics::mape(actual, predicted);
        if (mape.has_value()) {
            result_data[i] = mape.value();
        } else {
            result_validity.SetInvalid(i);
        }
    }
}

// TS_SMAPE(actual DOUBLE[], predicted DOUBLE[]) -> DOUBLE
static void TSSMAPEFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &actual_vec = args.data[0];
    auto &predicted_vec = args.data[1];
    
    UnifiedVectorFormat actual_format, predicted_format;
    actual_vec.ToUnifiedFormat(args.size(), actual_format);
    predicted_vec.ToUnifiedFormat(args.size(), predicted_format);
    
    auto result_data = FlatVector::GetData<double>(result);
    auto &result_validity = FlatVector::Validity(result);
    
    for (idx_t i = 0; i < args.size(); i++) {
        auto actual_idx = actual_format.sel->get_index(i);
        auto predicted_idx = predicted_format.sel->get_index(i);
        
        if (!actual_format.validity.RowIsValid(actual_idx) || 
            !predicted_format.validity.RowIsValid(predicted_idx)) {
            result_validity.SetInvalid(i);
            continue;
        }
        
        auto actual = ExtractDoubleArray(actual_vec, actual_idx, actual_format);
        auto predicted = ExtractDoubleArray(predicted_vec, predicted_idx, predicted_format);
        
        if (actual.size() != predicted.size()) {
            throw InvalidInputException("actual and predicted arrays must have the same length");
        }
        if (actual.empty()) {
            throw InvalidInputException("arrays must not be empty");
        }
        
        auto smape = ::anofoxtime::utils::Metrics::smape(actual, predicted);
        if (smape.has_value()) {
            result_data[i] = smape.value();
        } else {
            result_validity.SetInvalid(i);
        }
    }
}

// TS_MASE(actual DOUBLE[], predicted DOUBLE[], baseline DOUBLE[]) -> DOUBLE
static void TSMASEFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &actual_vec = args.data[0];
    auto &predicted_vec = args.data[1];
    auto &baseline_vec = args.data[2];
    
    UnifiedVectorFormat actual_format, predicted_format, baseline_format;
    actual_vec.ToUnifiedFormat(args.size(), actual_format);
    predicted_vec.ToUnifiedFormat(args.size(), predicted_format);
    baseline_vec.ToUnifiedFormat(args.size(), baseline_format);
    
    auto result_data = FlatVector::GetData<double>(result);
    auto &result_validity = FlatVector::Validity(result);
    
    for (idx_t i = 0; i < args.size(); i++) {
        auto actual_idx = actual_format.sel->get_index(i);
        auto predicted_idx = predicted_format.sel->get_index(i);
        auto baseline_idx = baseline_format.sel->get_index(i);
        
        if (!actual_format.validity.RowIsValid(actual_idx) || 
            !predicted_format.validity.RowIsValid(predicted_idx) ||
            !baseline_format.validity.RowIsValid(baseline_idx)) {
            result_validity.SetInvalid(i);
            continue;
        }
        
        auto actual = ExtractDoubleArray(actual_vec, actual_idx, actual_format);
        auto predicted = ExtractDoubleArray(predicted_vec, predicted_idx, predicted_format);
        auto baseline = ExtractDoubleArray(baseline_vec, baseline_idx, baseline_format);
        
        if (actual.size() != predicted.size() || actual.size() != baseline.size()) {
            throw InvalidInputException("actual, predicted, and baseline arrays must have the same length");
        }
        if (actual.empty()) {
            throw InvalidInputException("arrays must not be empty");
        }
        
        auto mase = ::anofoxtime::utils::Metrics::mase(actual, predicted, baseline);
        if (mase.has_value()) {
            result_data[i] = mase.value();
        } else {
            result_validity.SetInvalid(i);
        }
    }
}

// TS_R2(actual DOUBLE[], predicted DOUBLE[]) -> DOUBLE
static void TSR2Function(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &actual_vec = args.data[0];
    auto &predicted_vec = args.data[1];
    
    UnifiedVectorFormat actual_format, predicted_format;
    actual_vec.ToUnifiedFormat(args.size(), actual_format);
    predicted_vec.ToUnifiedFormat(args.size(), predicted_format);
    
    auto result_data = FlatVector::GetData<double>(result);
    auto &result_validity = FlatVector::Validity(result);
    
    for (idx_t i = 0; i < args.size(); i++) {
        auto actual_idx = actual_format.sel->get_index(i);
        auto predicted_idx = predicted_format.sel->get_index(i);
        
        if (!actual_format.validity.RowIsValid(actual_idx) || 
            !predicted_format.validity.RowIsValid(predicted_idx)) {
            result_validity.SetInvalid(i);
            continue;
        }
        
        auto actual = ExtractDoubleArray(actual_vec, actual_idx, actual_format);
        auto predicted = ExtractDoubleArray(predicted_vec, predicted_idx, predicted_format);
        
        if (actual.size() != predicted.size()) {
            throw InvalidInputException("actual and predicted arrays must have the same length");
        }
        if (actual.empty()) {
            throw InvalidInputException("arrays must not be empty");
        }
        
        auto r2 = ::anofoxtime::utils::Metrics::r2(actual, predicted);
        if (r2.has_value()) {
            result_data[i] = r2.value();
        } else {
            result_validity.SetInvalid(i);
        }
    }
}

// TS_BIAS(actual DOUBLE[], predicted DOUBLE[]) -> DOUBLE
static void TSBiasFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &actual_vec = args.data[0];
    auto &predicted_vec = args.data[1];
    
    UnifiedVectorFormat actual_format, predicted_format;
    actual_vec.ToUnifiedFormat(args.size(), actual_format);
    predicted_vec.ToUnifiedFormat(args.size(), predicted_format);
    
    auto result_data = FlatVector::GetData<double>(result);
    auto &result_validity = FlatVector::Validity(result);
    
    for (idx_t i = 0; i < args.size(); i++) {
        auto actual_idx = actual_format.sel->get_index(i);
        auto predicted_idx = predicted_format.sel->get_index(i);
        
        if (!actual_format.validity.RowIsValid(actual_idx) || 
            !predicted_format.validity.RowIsValid(predicted_idx)) {
            result_validity.SetInvalid(i);
            continue;
        }
        
        auto actual = ExtractDoubleArray(actual_vec, actual_idx, actual_format);
        auto predicted = ExtractDoubleArray(predicted_vec, predicted_idx, predicted_format);
        
        if (actual.size() != predicted.size()) {
            throw InvalidInputException("actual and predicted arrays must have the same length");
        }
        if (actual.empty()) {
            throw InvalidInputException("arrays must not be empty");
        }
        
        // Calculate bias: mean(predicted - actual)
        double sum = 0.0;
        for (size_t j = 0; j < actual.size(); j++) {
            sum += (predicted[j] - actual[j]);
        }
        result_data[i] = sum / actual.size();
    }
}

void RegisterMetricsFunction(ExtensionLoader &loader) {
    // TS_MAE
    ScalarFunction ts_mae("ts_mae",
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::DOUBLE,
        TSMAEFunction
    );
    loader.RegisterFunction(ts_mae);
    
    // TS_MSE
    ScalarFunction ts_mse("ts_mse",
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::DOUBLE,
        TSMSEFunction
    );
    loader.RegisterFunction(ts_mse);
    
    // TS_RMSE
    ScalarFunction ts_rmse("ts_rmse",
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::DOUBLE,
        TSRMSEFunction
    );
    loader.RegisterFunction(ts_rmse);
    
    // TS_MAPE
    ScalarFunction ts_mape("ts_mape",
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::DOUBLE,
        TSMAPEFunction
    );
    loader.RegisterFunction(ts_mape);
    
    // TS_SMAPE  
    ScalarFunction ts_smape("ts_smape",
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::DOUBLE,
        TSSMAPEFunction
    );
    loader.RegisterFunction(ts_smape);
    
    // TS_MASE (requires baseline)
    ScalarFunction ts_mase("ts_mase",
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE), 
         LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::DOUBLE,
        TSMASEFunction
    );
    loader.RegisterFunction(ts_mase);
    
    // TS_R2
    ScalarFunction ts_r2("ts_r2",
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::DOUBLE,
        TSR2Function
    );
    loader.RegisterFunction(ts_r2);
    
    // TS_BIAS
    ScalarFunction ts_bias("ts_bias",
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::DOUBLE,
        TSBiasFunction
    );
    loader.RegisterFunction(ts_bias);
}

} // namespace duckdb
