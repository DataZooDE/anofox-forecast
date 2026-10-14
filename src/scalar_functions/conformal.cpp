#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"

#include "duckdb/function/scalar_function.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include "duckdb/common/vector_operations/generic_executor.hpp"
#include "duckdb/common/types/vector.hpp"
#include "ts_list_marshal.hpp"

namespace duckdb {

// Writes `value` into row `row_idx` of a DOUBLE result child, or SQL NULL if
// `value` is NaN (Phase 16: coverage/conformity_score can be NaN when
// calibration is insufficient or there is no calibration data at all).
// Forward-declared here (defined below, near the from-pairs/calibrate
// internal functions) so every conformal scalar can share it.
static void WriteDoubleOrNull(Vector &vec, idx_t row_idx, double value);

// ============================================================================
// ts_conformal_quantile - Compute conformity score from residuals
// ts_conformal_quantile(residuals[], alpha) -> DOUBLE
// ============================================================================

static void TsConformalQuantileFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &residuals_vec = args.data[0];
    auto &alpha_vec = args.data[1];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto result_data = FlatVector::GetData<double>(result);

    ListDoubleReader residuals_reader(residuals_vec, count);
    ScalarArgReader<double> alpha_reader(alpha_vec, count);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (residuals_reader.IsNull(row_idx) || alpha_reader.IsNull(row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> residuals;
        vector<uint64_t> residuals_validity;
        residuals_reader.Read(row_idx, residuals, residuals_validity);
        double alpha = alpha_reader.Get(row_idx);

        AnofoxError error;
        double quantile_result;
        bool success = anofox_ts_conformal_quantile(
            residuals.data(), residuals_validity.empty() ? nullptr : residuals_validity.data(),
            residuals.size(), alpha, &quantile_result, &error
        );

        if (!success) {
            if (error.code == INVALID_INPUT) {
                throw InvalidInputException("ts_conformal_quantile: %s", error.message);
            }
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // Phase 16 D-05: an insufficient calibration set reports NaN here
        // (unbounded quantile) -- surface it as SQL NULL, not NaN.
        if (std::isnan(quantile_result)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        result_data[row_idx] = quantile_result;
    }
}

void RegisterTsConformalQuantileFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_cq_set("ts_conformal_quantile");
    ts_cq_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE)},
        LogicalType(LogicalTypeId::DOUBLE),
        TsConformalQuantileFunction
    ));
    {
        CreateScalarFunctionInfo info(ts_cq_set);
        FunctionDescription desc;
        desc.description = "Computes the conformal quantile from calibration nonconformity scores at a given miscoverage rate alpha.";
        desc.examples = {"ts_conformal_quantile(calibration_scores, 0.1)"};
        desc.categories = {"time-series", "uncertainty", "conformal-prediction"};
        desc.parameter_names = {"calibration_scores", "alpha"};
        desc.parameter_types = {
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType(LogicalTypeId::DOUBLE)
        };
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

    ScalarFunctionSet anofox_set("anofox_fcst_ts_conformal_quantile");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE)},
        LogicalType(LogicalTypeId::DOUBLE),
        TsConformalQuantileFunction
    ));
    {
        CreateScalarFunctionInfo info(anofox_set);
        info.alias_of = "ts_conformal_quantile";
        FunctionDescription desc;
        desc.description = "Computes the conformal quantile from calibration nonconformity scores at a given miscoverage rate alpha.";
        desc.examples = {"ts_conformal_quantile(calibration_scores, 0.1)"};
        desc.categories = {"time-series", "uncertainty", "conformal-prediction"};
        desc.parameter_names = {"calibration_scores", "alpha"};
        desc.parameter_types = {
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType(LogicalTypeId::DOUBLE)
        };
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

// ============================================================================
// ts_conformal_intervals - Apply conformity score to create intervals
// ts_conformal_intervals(forecasts[], conformity_score) -> STRUCT(lower[], upper[])
// ============================================================================

static void TsConformalIntervalsFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &forecasts_vec = args.data[0];
    auto &score_vec = args.data[1];
    idx_t count = args.size();

    ListDoubleReader forecasts_reader(forecasts_vec, count);
    ScalarArgReader<double> score_reader(score_vec, count);

    // Result is a STRUCT with lower and upper arrays
    auto &struct_entries = StructVector::GetEntries(result);
    auto &lower_vec = *struct_entries[0];
    auto &upper_vec = *struct_entries[1];

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (forecasts_reader.IsNull(row_idx) || score_reader.IsNull(row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> forecasts;
        vector<uint64_t> forecasts_validity;
        forecasts_reader.Read(row_idx, forecasts, forecasts_validity);
        double conformity_score = score_reader.Get(row_idx);

        AnofoxError error;
        double *out_lower = nullptr;
        double *out_upper = nullptr;
        bool success = anofox_ts_conformal_intervals(
            forecasts.data(), forecasts_validity.empty() ? nullptr : forecasts_validity.data(),
            forecasts.size(), conformity_score,
            &out_lower, &out_upper, &error
        );

        if (!success || out_lower == nullptr || out_upper == nullptr) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        WriteDoubleListNanAsNull(lower_vec, row_idx, out_lower, forecasts.size());
        WriteDoubleListNanAsNull(upper_vec, row_idx, out_upper, forecasts.size());

        // Free FFI memory
        anofox_free_double_array(out_lower);
        anofox_free_double_array(out_upper);
    }
}

void RegisterTsConformalIntervalsFunction(ExtensionLoader &loader) {
    child_list_t<LogicalType> struct_children;
    struct_children.push_back(make_pair("lower", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    struct_children.push_back(make_pair("upper", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    auto result_type = LogicalType::STRUCT(std::move(struct_children));

    ScalarFunctionSet ts_ci_set("ts_conformal_intervals");
    ts_ci_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE)},
        result_type,
        TsConformalIntervalsFunction
    ));
    {
        CreateScalarFunctionInfo info(ts_ci_set);
        FunctionDescription desc;
        desc.description = "Computes symmetric conformal prediction intervals by applying a conformal quantile to point forecasts.";
        desc.examples = {"ts_conformal_intervals(point_forecast, conformal_q)"};
        desc.categories = {"time-series", "uncertainty", "conformal-prediction"};
        desc.parameter_names = {"point_forecast", "conformal_q"};
        desc.parameter_types = {
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType(LogicalTypeId::DOUBLE)
        };
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

    ScalarFunctionSet anofox_set("anofox_fcst_ts_conformal_intervals");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE)},
        result_type,
        TsConformalIntervalsFunction
    ));
    {
        CreateScalarFunctionInfo info(anofox_set);
        info.alias_of = "ts_conformal_intervals";
        FunctionDescription desc;
        desc.description = "Computes symmetric conformal prediction intervals by applying a conformal quantile to point forecasts.";
        desc.examples = {"ts_conformal_intervals(point_forecast, conformal_q)"};
        desc.categories = {"time-series", "uncertainty", "conformal-prediction"};
        desc.parameter_names = {"point_forecast", "conformal_q"};
        desc.parameter_types = {
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType(LogicalTypeId::DOUBLE)
        };
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

// ============================================================================
// ts_conformal_predict - Full conformal prediction in one call
// ts_conformal_predict(residuals[], forecasts[], alpha) -> STRUCT
// ============================================================================

static void TsConformalPredictFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &residuals_vec = args.data[0];
    auto &forecasts_vec = args.data[1];
    auto &alpha_vec = args.data[2];
    idx_t count = args.size();

    ListDoubleReader residuals_reader(residuals_vec, count);
    ListDoubleReader forecasts_reader(forecasts_vec, count);
    ScalarArgReader<double> alpha_reader(alpha_vec, count);

    // Result struct fields
    auto &struct_entries = StructVector::GetEntries(result);
    auto &point_vec = *struct_entries[0];
    auto &lower_vec = *struct_entries[1];
    auto &upper_vec = *struct_entries[2];
    auto &coverage_vec = *struct_entries[3];
    auto &score_vec = *struct_entries[4];
    auto &method_vec = *struct_entries[5];
    auto &nominal_level_vec = *struct_entries[6];
    auto &n_calibration_vec = *struct_entries[7];
    auto &calibration_insufficient_vec = *struct_entries[8];

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (residuals_reader.IsNull(row_idx) || forecasts_reader.IsNull(row_idx) ||
            alpha_reader.IsNull(row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> residuals, forecasts;
        vector<uint64_t> residuals_validity, forecasts_validity;
        residuals_reader.Read(row_idx, residuals, residuals_validity);
        forecasts_reader.Read(row_idx, forecasts, forecasts_validity);
        double alpha = alpha_reader.Get(row_idx);

        ConformalResultFFI conf_result = {};
        AnofoxError error;
        bool success = anofox_ts_conformal_predict(
            residuals.data(), residuals_validity.empty() ? nullptr : residuals_validity.data(),
            residuals.size(),
            forecasts.data(), forecasts_validity.empty() ? nullptr : forecasts_validity.data(),
            forecasts.size(),
            alpha, &conf_result, &error
        );

        if (!success) {
            if (error.code == INVALID_INPUT) {
                throw InvalidInputException("ts_conformal_predict: %s", error.message);
            }
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        WriteDoubleListNanAsNull(point_vec, row_idx, conf_result.point, conf_result.n_forecasts);
        WriteDoubleListNanAsNull(lower_vec, row_idx, conf_result.lower, conf_result.n_forecasts);
        WriteDoubleListNanAsNull(upper_vec, row_idx, conf_result.upper, conf_result.n_forecasts);

        // Scalar fields
        WriteDoubleOrNull(coverage_vec, row_idx, conf_result.coverage);
        WriteDoubleOrNull(score_vec, row_idx, conf_result.conformity_score);
        FlatVector::GetData<string_t>(method_vec)[row_idx] = StringVector::AddString(method_vec, conf_result.method);
        FlatVector::GetData<double>(nominal_level_vec)[row_idx] = conf_result.nominal_level;
        FlatVector::GetData<int64_t>(n_calibration_vec)[row_idx] = (int64_t)conf_result.n_calibration;
        FlatVector::GetData<bool>(calibration_insufficient_vec)[row_idx] = conf_result.calibration_insufficient;

        // Free FFI memory
        anofox_free_conformal_result(&conf_result);
    }
}

void RegisterTsConformalPredictFunction(ExtensionLoader &loader) {
    child_list_t<LogicalType> struct_children;
    struct_children.push_back(make_pair("point", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    struct_children.push_back(make_pair("lower", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    struct_children.push_back(make_pair("upper", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    struct_children.push_back(make_pair("coverage", LogicalType(LogicalTypeId::DOUBLE)));
    struct_children.push_back(make_pair("conformity_score", LogicalType(LogicalTypeId::DOUBLE)));
    struct_children.push_back(make_pair("method", LogicalType(LogicalTypeId::VARCHAR)));
    struct_children.push_back(make_pair("nominal_level", LogicalType(LogicalTypeId::DOUBLE)));
    struct_children.push_back(make_pair("n_calibration", LogicalType(LogicalTypeId::BIGINT)));
    struct_children.push_back(make_pair("calibration_insufficient", LogicalType(LogicalTypeId::BOOLEAN)));
    auto result_type = LogicalType::STRUCT(std::move(struct_children));

    ScalarFunctionSet ts_cp_set("ts_conformal_predict");
    ts_cp_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE)},
        result_type,
        TsConformalPredictFunction
    ));
    {
        CreateScalarFunctionInfo info(ts_cp_set);
        FunctionDescription desc;
        desc.description = "End-to-end symmetric conformal prediction: calibrates on residuals and applies to forecasts in one step.";
        desc.examples = {"ts_conformal_predict(calibration_actuals, calibration_forecasts, point_forecast, 0.1)"};
        desc.categories = {"time-series", "uncertainty", "conformal-prediction"};
        desc.parameter_names = {"residuals", "forecasts", "alpha"};
        desc.parameter_types = {
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType(LogicalTypeId::DOUBLE)
        };
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

    ScalarFunctionSet anofox_set("anofox_fcst_ts_conformal_predict");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE)},
        result_type,
        TsConformalPredictFunction
    ));
    {
        CreateScalarFunctionInfo info(anofox_set);
        info.alias_of = "ts_conformal_predict";
        FunctionDescription desc;
        desc.description = "End-to-end symmetric conformal prediction: calibrates on residuals and applies to forecasts in one step.";
        desc.examples = {"ts_conformal_predict(calibration_actuals, calibration_forecasts, point_forecast, 0.1)"};
        desc.categories = {"time-series", "uncertainty", "conformal-prediction"};
        desc.parameter_names = {"residuals", "forecasts", "alpha"};
        desc.parameter_types = {
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType(LogicalTypeId::DOUBLE)
        };
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

// ============================================================================
// ts_conformal_predict_asymmetric - Asymmetric conformal prediction
// ts_conformal_predict_asymmetric(residuals[], forecasts[], alpha) -> STRUCT
// ============================================================================

static void TsConformalPredictAsymmetricFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &residuals_vec = args.data[0];
    auto &forecasts_vec = args.data[1];
    auto &alpha_vec = args.data[2];
    idx_t count = args.size();

    ListDoubleReader residuals_reader(residuals_vec, count);
    ListDoubleReader forecasts_reader(forecasts_vec, count);
    ScalarArgReader<double> alpha_reader(alpha_vec, count);

    // Result struct fields
    auto &struct_entries = StructVector::GetEntries(result);
    auto &point_vec = *struct_entries[0];
    auto &lower_vec = *struct_entries[1];
    auto &upper_vec = *struct_entries[2];
    auto &coverage_vec = *struct_entries[3];
    auto &score_vec = *struct_entries[4];
    auto &method_vec = *struct_entries[5];
    auto &nominal_level_vec = *struct_entries[6];
    auto &n_calibration_vec = *struct_entries[7];
    auto &calibration_insufficient_vec = *struct_entries[8];

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (residuals_reader.IsNull(row_idx) || forecasts_reader.IsNull(row_idx) ||
            alpha_reader.IsNull(row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> residuals, forecasts;
        vector<uint64_t> residuals_validity, forecasts_validity;
        residuals_reader.Read(row_idx, residuals, residuals_validity);
        forecasts_reader.Read(row_idx, forecasts, forecasts_validity);
        double alpha = alpha_reader.Get(row_idx);

        ConformalResultFFI conf_result = {};
        AnofoxError error;
        bool success = anofox_ts_conformal_predict_asymmetric(
            residuals.data(), residuals_validity.empty() ? nullptr : residuals_validity.data(),
            residuals.size(),
            forecasts.data(), forecasts_validity.empty() ? nullptr : forecasts_validity.data(),
            forecasts.size(),
            alpha, &conf_result, &error
        );

        if (!success) {
            if (error.code == INVALID_INPUT) {
                throw InvalidInputException("ts_conformal_predict_asymmetric: %s", error.message);
            }
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        WriteDoubleListNanAsNull(point_vec, row_idx, conf_result.point, conf_result.n_forecasts);
        WriteDoubleListNanAsNull(lower_vec, row_idx, conf_result.lower, conf_result.n_forecasts);
        WriteDoubleListNanAsNull(upper_vec, row_idx, conf_result.upper, conf_result.n_forecasts);

        // Scalar fields
        WriteDoubleOrNull(coverage_vec, row_idx, conf_result.coverage);
        WriteDoubleOrNull(score_vec, row_idx, conf_result.conformity_score);
        FlatVector::GetData<string_t>(method_vec)[row_idx] = StringVector::AddString(method_vec, conf_result.method);
        FlatVector::GetData<double>(nominal_level_vec)[row_idx] = conf_result.nominal_level;
        FlatVector::GetData<int64_t>(n_calibration_vec)[row_idx] = (int64_t)conf_result.n_calibration;
        FlatVector::GetData<bool>(calibration_insufficient_vec)[row_idx] = conf_result.calibration_insufficient;

        // Free FFI memory
        anofox_free_conformal_result(&conf_result);
    }
}

void RegisterTsConformalPredictAsymmetricFunction(ExtensionLoader &loader) {
    child_list_t<LogicalType> struct_children;
    struct_children.push_back(make_pair("point", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    struct_children.push_back(make_pair("lower", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    struct_children.push_back(make_pair("upper", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    struct_children.push_back(make_pair("coverage", LogicalType(LogicalTypeId::DOUBLE)));
    struct_children.push_back(make_pair("conformity_score", LogicalType(LogicalTypeId::DOUBLE)));
    struct_children.push_back(make_pair("method", LogicalType(LogicalTypeId::VARCHAR)));
    struct_children.push_back(make_pair("nominal_level", LogicalType(LogicalTypeId::DOUBLE)));
    struct_children.push_back(make_pair("n_calibration", LogicalType(LogicalTypeId::BIGINT)));
    struct_children.push_back(make_pair("calibration_insufficient", LogicalType(LogicalTypeId::BOOLEAN)));
    auto result_type = LogicalType::STRUCT(std::move(struct_children));

    ScalarFunctionSet ts_cpa_set("ts_conformal_predict_asymmetric");
    ts_cpa_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE)},
        result_type,
        TsConformalPredictAsymmetricFunction
    ));
    {
        CreateScalarFunctionInfo info(ts_cpa_set);
        FunctionDescription desc;
        desc.description = "End-to-end asymmetric conformal prediction using separate upper and lower quantiles for skewed errors.";
        desc.examples = {"ts_conformal_predict_asymmetric(calibration_actuals, calibration_forecasts, point_forecast, 0.1)"};
        desc.categories = {"time-series", "uncertainty", "conformal-prediction"};
        desc.parameter_names = {"residuals", "forecasts", "alpha"};
        desc.parameter_types = {
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType(LogicalTypeId::DOUBLE)
        };
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

    ScalarFunctionSet anofox_set("anofox_fcst_ts_conformal_predict_asymmetric");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE)},
        result_type,
        TsConformalPredictAsymmetricFunction
    ));
    {
        CreateScalarFunctionInfo info(anofox_set);
        info.alias_of = "ts_conformal_predict_asymmetric";
        FunctionDescription desc;
        desc.description = "End-to-end asymmetric conformal prediction using separate upper and lower quantiles for skewed errors.";
        desc.examples = {"ts_conformal_predict_asymmetric(calibration_actuals, calibration_forecasts, point_forecast, 0.1)"};
        desc.categories = {"time-series", "uncertainty", "conformal-prediction"};
        desc.parameter_names = {"residuals", "forecasts", "alpha"};
        desc.parameter_types = {
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType(LogicalTypeId::DOUBLE)
        };
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

// ============================================================================
// ts_conformal_learn - Learn a calibration profile from residuals
// ts_conformal_learn(residuals[], alphas[], method, strategy) -> STRUCT
// ============================================================================

static ConformalMethodFFI ParseConformalMethod(const string &method_str) {
    if (method_str == "symmetric" || method_str == "Symmetric") {
        return SYMMETRIC;
    } else if (method_str == "asymmetric" || method_str == "Asymmetric") {
        return ASYMMETRIC;
    } else if (method_str == "adaptive" || method_str == "Adaptive") {
        return ADAPTIVE;
    }
    return SYMMETRIC;  // default
}

static ConformalStrategyFFI ParseConformalStrategy(const string &strategy_str) {
    if (strategy_str == "split" || strategy_str == "Split") {
        return SPLIT;
    } else if (strategy_str == "crossval" || strategy_str == "CrossVal" || strategy_str == "cross_val") {
        return CROSS_VAL;
    } else if (strategy_str == "jackknife_plus" || strategy_str == "JackknifePlus" || strategy_str == "jackknife+") {
        return JACKKNIFE_PLUS;
    }
    return SPLIT;  // default
}

static const char* MethodToString(ConformalMethodFFI method) {
    switch (method) {
        case SYMMETRIC: return "symmetric";
        case ASYMMETRIC: return "asymmetric";
        case ADAPTIVE: return "adaptive";
    }
    return "symmetric";
}

static const char* StrategyToString(ConformalStrategyFFI strategy) {
    switch (strategy) {
        case SPLIT: return "split";
        case CROSS_VAL: return "crossval";
        case JACKKNIFE_PLUS: return "jackknife_plus";
    }
    return "split";
}

static void TsConformalLearnFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &residuals_vec = args.data[0];
    auto &alphas_vec = args.data[1];
    auto &method_vec = args.data[2];
    auto &strategy_vec = args.data[3];
    idx_t count = args.size();

    ListDoubleReader residuals_reader(residuals_vec, count);
    ListDoubleReader alphas_reader(alphas_vec, count);
    ScalarArgReader<string_t> method_reader(method_vec, count);
    ScalarArgReader<string_t> strategy_reader(strategy_vec, count);

    // Result struct fields
    auto &struct_entries = StructVector::GetEntries(result);
    auto &out_method_vec = *struct_entries[0];
    auto &out_strategy_vec = *struct_entries[1];
    auto &out_alphas_vec = *struct_entries[2];
    auto &out_state_vec = *struct_entries[3];
    auto &out_scores_lower_vec = *struct_entries[4];
    auto &out_scores_upper_vec = *struct_entries[5];
    auto &out_n_residuals_vec = *struct_entries[6];

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (residuals_reader.IsNull(row_idx) || alphas_reader.IsNull(row_idx) ||
            method_reader.IsNull(row_idx) || strategy_reader.IsNull(row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> residuals, alphas;
        vector<uint64_t> residuals_validity;
        residuals_reader.Read(row_idx, residuals, residuals_validity);
        alphas_reader.ReadNoNulls(row_idx, alphas, "ts_conformal_learn", "alphas");

        auto method_str = method_reader.Get(row_idx).GetString();
        auto strategy_str = strategy_reader.Get(row_idx).GetString();

        ConformalMethodFFI method = ParseConformalMethod(method_str);
        ConformalStrategyFFI strategy = ParseConformalStrategy(strategy_str);

        CalibrationProfileFFI profile = {};
        AnofoxError error;
        bool success = anofox_ts_conformal_learn(
            residuals.data(), residuals_validity.empty() ? nullptr : residuals_validity.data(),
            residuals.size(),
            alphas.data(), alphas.size(),
            method, strategy,
            nullptr,  // no difficulty scores
            &profile, &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // Build method string
        FlatVector::GetData<string_t>(out_method_vec)[row_idx] = StringVector::AddString(out_method_vec, MethodToString(profile.method));

        // Build strategy string
        FlatVector::GetData<string_t>(out_strategy_vec)[row_idx] = StringVector::AddString(out_strategy_vec, StrategyToString(profile.strategy));

        WriteDoubleList(out_alphas_vec, row_idx, profile.alphas, profile.n_levels);
        WriteDoubleList(out_state_vec, row_idx, profile.state_vector, profile.state_vector_len);
        WriteDoubleList(out_scores_lower_vec, row_idx, profile.scores_lower, profile.n_levels);
        WriteDoubleList(out_scores_upper_vec, row_idx, profile.scores_upper, profile.n_levels);

        // n_residuals
        FlatVector::GetData<int64_t>(out_n_residuals_vec)[row_idx] = static_cast<int64_t>(profile.n_residuals);

        // Free FFI memory
        anofox_free_calibration_profile(&profile);
    }
}

void RegisterTsConformalLearnFunction(ExtensionLoader &loader) {
    child_list_t<LogicalType> struct_children;
    struct_children.push_back(make_pair("method", LogicalType(LogicalTypeId::VARCHAR)));
    struct_children.push_back(make_pair("strategy", LogicalType(LogicalTypeId::VARCHAR)));
    struct_children.push_back(make_pair("alphas", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    struct_children.push_back(make_pair("state_vector", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    struct_children.push_back(make_pair("scores_lower", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    struct_children.push_back(make_pair("scores_upper", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    struct_children.push_back(make_pair("n_residuals", LogicalType(LogicalTypeId::BIGINT)));
    auto result_type = LogicalType::STRUCT(std::move(struct_children));

    ScalarFunctionSet ts_cl_set("ts_conformal_learn");
    ts_cl_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::VARCHAR), LogicalType(LogicalTypeId::VARCHAR)},
        result_type,
        TsConformalLearnFunction
    ));
    {
        CreateScalarFunctionInfo info(ts_cl_set);
        FunctionDescription desc;
        desc.description = "Calibrates a conformal predictor from calibration set actuals and forecasts. Returns conformal quantile for reuse.";
        desc.examples = {"ts_conformal_learn(LIST(actual ORDER BY date), LIST(forecast ORDER BY date), 0.1)"};
        desc.categories = {"time-series", "uncertainty", "conformal-prediction"};
        desc.parameter_names = {"residuals", "alphas", "method", "strategy"};
        desc.parameter_types = {
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType(LogicalTypeId::VARCHAR),
            LogicalType(LogicalTypeId::VARCHAR)
        };
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

    ScalarFunctionSet anofox_set("anofox_fcst_ts_conformal_learn");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::VARCHAR), LogicalType(LogicalTypeId::VARCHAR)},
        result_type,
        TsConformalLearnFunction
    ));
    {
        CreateScalarFunctionInfo info(anofox_set);
        info.alias_of = "ts_conformal_learn";
        FunctionDescription desc;
        desc.description = "Calibrates a conformal predictor from calibration set actuals and forecasts. Returns conformal quantile for reuse.";
        desc.examples = {"ts_conformal_learn(LIST(actual ORDER BY date), LIST(forecast ORDER BY date), 0.1)"};
        desc.categories = {"time-series", "uncertainty", "conformal-prediction"};
        desc.parameter_names = {"residuals", "alphas", "method", "strategy"};
        desc.parameter_types = {
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType(LogicalTypeId::VARCHAR),
            LogicalType(LogicalTypeId::VARCHAR)
        };
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

// ============================================================================
// ts_conformal_apply - Apply a calibration profile to forecasts
// ts_conformal_apply(forecasts[], profile_struct) -> STRUCT
// ============================================================================

static void TsConformalApplyFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &forecasts_vec = args.data[0];
    auto &profile_vec = args.data[1];
    idx_t count = args.size();

    // Flatten the profile STRUCT argument first so its child vectors are
    // indexable per row even when the whole STRUCT is constant/dictionary
    // (T-12-15).
    profile_vec.Flatten(count);

    ListDoubleReader forecasts_reader(forecasts_vec, count);

    // Profile struct fields
    auto &profile_entries = StructVector::GetEntries(profile_vec);
    auto &method_vec = *profile_entries[0];
    auto &strategy_vec_input = *profile_entries[1];
    auto &alphas_vec = *profile_entries[2];
    auto &state_vec = *profile_entries[3];
    auto &scores_lower_vec = *profile_entries[4];
    auto &scores_upper_vec = *profile_entries[5];
    auto &n_residuals_vec = *profile_entries[6];

    ListDoubleReader alphas_reader(alphas_vec, count);
    ListDoubleReader state_reader(state_vec, count);
    ListDoubleReader scores_lower_reader(scores_lower_vec, count);
    ListDoubleReader scores_upper_reader(scores_upper_vec, count);

    // Result struct fields
    auto &out_entries = StructVector::GetEntries(result);
    auto &out_point_vec = *out_entries[0];
    auto &out_coverage_vec = *out_entries[1];
    auto &out_lower_vec = *out_entries[2];
    auto &out_upper_vec = *out_entries[3];
    auto &out_method_vec = *out_entries[4];

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (forecasts_reader.IsNull(row_idx) || FlatVector::IsNull(profile_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> forecasts;
        vector<uint64_t> forecasts_validity;
        forecasts_reader.Read(row_idx, forecasts, forecasts_validity);

        // Extract profile fields (flat after profile_vec.Flatten(count) above).
        auto method_str = FlatVector::GetData<string_t>(method_vec)[row_idx].GetString();
        auto strategy_str = FlatVector::GetData<string_t>(strategy_vec_input)[row_idx].GetString();
        auto n_residuals = FlatVector::GetData<int64_t>(n_residuals_vec)[row_idx];

        vector<double> alphas, state_vector, scores_lower, scores_upper;
        alphas_reader.ReadNoNulls(row_idx, alphas, "ts_conformal_apply", "profile.alphas");
        state_reader.ReadNoNulls(row_idx, state_vector, "ts_conformal_apply", "profile.state_vector");
        scores_lower_reader.ReadNoNulls(row_idx, scores_lower, "ts_conformal_apply", "profile.scores_lower");
        scores_upper_reader.ReadNoNulls(row_idx, scores_upper, "ts_conformal_apply", "profile.scores_upper");

        // Build CalibrationProfileFFI from the struct
        CalibrationProfileFFI profile = {};
        profile.method = ParseConformalMethod(method_str);
        profile.strategy = ParseConformalStrategy(strategy_str);
        profile.alphas = alphas.data();
        profile.state_vector = state_vector.data();
        profile.state_vector_len = state_vector.size();
        profile.scores_lower = scores_lower.data();
        profile.scores_upper = scores_upper.data();
        profile.n_levels = alphas.size();
        profile.n_residuals = static_cast<size_t>(n_residuals);

        PredictionIntervalsFFI intervals = {};
        AnofoxError error;
        bool success = anofox_ts_conformal_apply(
            forecasts.data(), forecasts_validity.empty() ? nullptr : forecasts_validity.data(),
            forecasts.size(),
            &profile,
            nullptr,  // no difficulty scores
            &intervals, &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        WriteDoubleListNanAsNull(out_point_vec, row_idx, intervals.point, intervals.n_forecasts);
        WriteDoubleList(out_coverage_vec, row_idx, intervals.coverage, intervals.n_levels);

        size_t total_lower = intervals.n_levels * intervals.n_forecasts;
        size_t total_upper = intervals.n_levels * intervals.n_forecasts;
        WriteDoubleListNanAsNull(out_lower_vec, row_idx, intervals.lower, total_lower);
        WriteDoubleListNanAsNull(out_upper_vec, row_idx, intervals.upper, total_upper);

        // Method string
        FlatVector::GetData<string_t>(out_method_vec)[row_idx] = StringVector::AddString(out_method_vec, MethodToString(intervals.method));

        // Free FFI memory
        anofox_free_prediction_intervals(&intervals);
    }
}

void RegisterTsConformalApplyFunction(ExtensionLoader &loader) {
    // Profile input type
    child_list_t<LogicalType> profile_children;
    profile_children.push_back(make_pair("method", LogicalType(LogicalTypeId::VARCHAR)));
    profile_children.push_back(make_pair("strategy", LogicalType(LogicalTypeId::VARCHAR)));
    profile_children.push_back(make_pair("alphas", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    profile_children.push_back(make_pair("state_vector", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    profile_children.push_back(make_pair("scores_lower", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    profile_children.push_back(make_pair("scores_upper", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    profile_children.push_back(make_pair("n_residuals", LogicalType(LogicalTypeId::BIGINT)));
    auto profile_type = LogicalType::STRUCT(std::move(profile_children));

    // Result type
    child_list_t<LogicalType> result_children;
    result_children.push_back(make_pair("point", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    result_children.push_back(make_pair("coverage", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    result_children.push_back(make_pair("lower", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    result_children.push_back(make_pair("upper", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    result_children.push_back(make_pair("method", LogicalType(LogicalTypeId::VARCHAR)));
    auto result_type = LogicalType::STRUCT(std::move(result_children));

    ScalarFunctionSet ts_ca_set("ts_conformal_apply");
    ts_ca_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), profile_type},
        result_type,
        TsConformalApplyFunction
    ));
    {
        CreateScalarFunctionInfo info(ts_ca_set);
        FunctionDescription desc;
        desc.description = "Applies a pre-calibrated conformal quantile to new point forecasts to produce prediction intervals.";
        desc.examples = {"ts_conformal_apply(point_forecast, conformal_q)"};
        desc.categories = {"time-series", "uncertainty", "conformal-prediction"};
        desc.parameter_names = {"point_forecast", "profile"};
        desc.parameter_types = {
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            profile_type
        };
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

    ScalarFunctionSet anofox_set("anofox_fcst_ts_conformal_apply");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), profile_type},
        result_type,
        TsConformalApplyFunction
    ));
    {
        CreateScalarFunctionInfo info(anofox_set);
        info.alias_of = "ts_conformal_apply";
        FunctionDescription desc;
        desc.description = "Applies a pre-calibrated conformal quantile to new point forecasts to produce prediction intervals.";
        desc.examples = {"ts_conformal_apply(point_forecast, conformal_q)"};
        desc.categories = {"time-series", "uncertainty", "conformal-prediction"};
        desc.parameter_names = {"point_forecast", "profile"};
        desc.parameter_types = {
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            profile_type
        };
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

// ============================================================================
// ts_conformal_coverage - Compute empirical coverage
// ts_conformal_coverage(actuals[], lower[], upper[]) -> DOUBLE
// ============================================================================

static void TsConformalCoverageFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &actuals_vec = args.data[0];
    auto &lower_vec = args.data[1];
    auto &upper_vec = args.data[2];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto result_data = FlatVector::GetData<double>(result);

    ListDoubleReader actuals_reader(actuals_vec, count);
    ListDoubleReader lower_reader(lower_vec, count);
    ListDoubleReader upper_reader(upper_vec, count);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (actuals_reader.IsNull(row_idx) || lower_reader.IsNull(row_idx) ||
            upper_reader.IsNull(row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> actuals, lower, upper;
        vector<uint64_t> actuals_validity, lower_validity, upper_validity;
        actuals_reader.Read(row_idx, actuals, actuals_validity);
        lower_reader.Read(row_idx, lower, lower_validity);
        upper_reader.Read(row_idx, upper, upper_validity);

        if (actuals.size() != lower.size() || actuals.size() != upper.size()) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        AnofoxError error;
        double coverage_result;
        bool success = anofox_ts_conformal_coverage(
            actuals.data(), actuals_validity.empty() ? nullptr : actuals_validity.data(),
            lower.data(), lower_validity.empty() ? nullptr : lower_validity.data(),
            upper.data(), upper_validity.empty() ? nullptr : upper_validity.data(),
            actuals.size(),
            &coverage_result, &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        result_data[row_idx] = coverage_result;
    }
}

void RegisterTsConformalCoverageFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_cc_set("ts_conformal_coverage");
    ts_cc_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType(LogicalTypeId::DOUBLE),
        TsConformalCoverageFunction
    ));
    {
        CreateScalarFunctionInfo info(ts_cc_set);
        FunctionDescription desc;
        desc.description = "Computes empirical coverage rate of conformal prediction intervals on a test set.";
        desc.examples = {"ts_conformal_coverage(actual, lower, upper)"};
        desc.categories = {"time-series", "uncertainty", "conformal-prediction"};
        desc.parameter_names = {"actual", "lower", "upper"};
        desc.parameter_types = {
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))
        };
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

    ScalarFunctionSet anofox_set("anofox_fcst_ts_conformal_coverage");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType(LogicalTypeId::DOUBLE),
        TsConformalCoverageFunction
    ));
    {
        CreateScalarFunctionInfo info(anofox_set);
        info.alias_of = "ts_conformal_coverage";
        FunctionDescription desc;
        desc.description = "Computes empirical coverage rate of conformal prediction intervals on a test set.";
        desc.examples = {"ts_conformal_coverage(actual, lower, upper)"};
        desc.categories = {"time-series", "uncertainty", "conformal-prediction"};
        desc.parameter_names = {"actual", "lower", "upper"};
        desc.parameter_types = {
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))
        };
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

// ============================================================================
// ts_conformal_evaluate - Comprehensive conformal evaluation
// ts_conformal_evaluate(actuals[], lower[], upper[], alpha) -> STRUCT
// ============================================================================

static void TsConformalEvaluateFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &actuals_vec = args.data[0];
    auto &lower_vec = args.data[1];
    auto &upper_vec = args.data[2];
    auto &alpha_vec = args.data[3];
    idx_t count = args.size();

    ListDoubleReader actuals_reader(actuals_vec, count);
    ListDoubleReader lower_reader(lower_vec, count);
    ListDoubleReader upper_reader(upper_vec, count);
    ScalarArgReader<double> alpha_reader(alpha_vec, count);

    // Result struct fields
    auto &struct_entries = StructVector::GetEntries(result);
    auto &out_coverage_vec = *struct_entries[0];
    auto &out_violation_vec = *struct_entries[1];
    auto &out_width_vec = *struct_entries[2];
    auto &out_winkler_vec = *struct_entries[3];
    auto &out_n_obs_vec = *struct_entries[4];

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (actuals_reader.IsNull(row_idx) || lower_reader.IsNull(row_idx) ||
            upper_reader.IsNull(row_idx) || alpha_reader.IsNull(row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> actuals, lower, upper;
        vector<uint64_t> actuals_validity, lower_validity, upper_validity;
        actuals_reader.Read(row_idx, actuals, actuals_validity);
        lower_reader.Read(row_idx, lower, lower_validity);
        upper_reader.Read(row_idx, upper, upper_validity);
        double alpha = alpha_reader.Get(row_idx);

        if (actuals.size() != lower.size() || actuals.size() != upper.size()) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        ConformalEvaluationFFI eval = {};
        AnofoxError error;
        bool success = anofox_ts_conformal_evaluate(
            actuals.data(), actuals_validity.empty() ? nullptr : actuals_validity.data(),
            lower.data(), lower_validity.empty() ? nullptr : lower_validity.data(),
            upper.data(), upper_validity.empty() ? nullptr : upper_validity.data(),
            actuals.size(),
            alpha, &eval, &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        FlatVector::GetData<double>(out_coverage_vec)[row_idx] = eval.coverage;
        FlatVector::GetData<double>(out_violation_vec)[row_idx] = eval.violation_rate;
        FlatVector::GetData<double>(out_width_vec)[row_idx] = eval.mean_width;
        FlatVector::GetData<double>(out_winkler_vec)[row_idx] = eval.winkler_score;
        FlatVector::GetData<int64_t>(out_n_obs_vec)[row_idx] = static_cast<int64_t>(eval.n_observations);
    }
}

void RegisterTsConformalEvaluateFunction(ExtensionLoader &loader) {
    child_list_t<LogicalType> struct_children;
    struct_children.push_back(make_pair("coverage", LogicalType(LogicalTypeId::DOUBLE)));
    struct_children.push_back(make_pair("violation_rate", LogicalType(LogicalTypeId::DOUBLE)));
    struct_children.push_back(make_pair("mean_width", LogicalType(LogicalTypeId::DOUBLE)));
    struct_children.push_back(make_pair("winkler_score", LogicalType(LogicalTypeId::DOUBLE)));
    struct_children.push_back(make_pair("n_observations", LogicalType(LogicalTypeId::BIGINT)));
    auto result_type = LogicalType::STRUCT(std::move(struct_children));

    ScalarFunctionSet ts_ce_set("ts_conformal_evaluate");
    ts_ce_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE)},
        result_type,
        TsConformalEvaluateFunction
    ));
    {
        CreateScalarFunctionInfo info(ts_ce_set);
        FunctionDescription desc;
        desc.description = "Evaluates conformal interval quality returning coverage, mean width, and efficiency metrics.";
        desc.examples = {"ts_conformal_evaluate(actual, lower, upper, 0.1)"};
        desc.categories = {"time-series", "uncertainty", "conformal-prediction"};
        desc.parameter_names = {"actual", "lower", "upper", "alpha"};
        desc.parameter_types = {
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType(LogicalTypeId::DOUBLE)
        };
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

    ScalarFunctionSet anofox_set("anofox_fcst_ts_conformal_evaluate");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE)},
        result_type,
        TsConformalEvaluateFunction
    ));
    {
        CreateScalarFunctionInfo info(anofox_set);
        info.alias_of = "ts_conformal_evaluate";
        FunctionDescription desc;
        desc.description = "Evaluates conformal interval quality returning coverage, mean width, and efficiency metrics.";
        desc.examples = {"ts_conformal_evaluate(actual, lower, upper, 0.1)"};
        desc.categories = {"time-series", "uncertainty", "conformal-prediction"};
        desc.parameter_names = {"actual", "lower", "upper", "alpha"};
        desc.parameter_types = {
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType(LogicalTypeId::DOUBLE)
        };
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

// ============================================================================
// ts_mean_interval_width - Compute mean interval width
// ts_mean_interval_width(lower[], upper[]) -> DOUBLE
// ============================================================================

static void TsMeanIntervalWidthFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &lower_vec = args.data[0];
    auto &upper_vec = args.data[1];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto result_data = FlatVector::GetData<double>(result);

    ListDoubleReader lower_reader(lower_vec, count);
    ListDoubleReader upper_reader(upper_vec, count);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (lower_reader.IsNull(row_idx) || upper_reader.IsNull(row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> lower, upper;
        vector<uint64_t> lower_validity, upper_validity;
        lower_reader.Read(row_idx, lower, lower_validity);
        upper_reader.Read(row_idx, upper, upper_validity);

        if (lower.size() != upper.size()) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        AnofoxError error;
        double miw_result;
        bool success = anofox_ts_mean_interval_width(
            lower.data(), lower_validity.empty() ? nullptr : lower_validity.data(),
            upper.data(), upper_validity.empty() ? nullptr : upper_validity.data(),
            lower.size(),
            &miw_result, &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        result_data[row_idx] = miw_result;
    }
}

void RegisterTsMeanIntervalWidthFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_miw_set("ts_mean_interval_width");
    ts_miw_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType(LogicalTypeId::DOUBLE),
        TsMeanIntervalWidthFunction
    ));
    {
        CreateScalarFunctionInfo info(ts_miw_set);
        FunctionDescription desc;
        desc.description = "Computes the mean width of prediction intervals: average of (upper - lower) across all steps.";
        desc.examples = {"ts_mean_interval_width(lower, upper)"};
        desc.categories = {"time-series", "uncertainty"};
        desc.parameter_names = {"lower", "upper"};
        desc.parameter_types = {
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))
        };
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

    ScalarFunctionSet anofox_set("anofox_fcst_ts_mean_interval_width");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType(LogicalTypeId::DOUBLE),
        TsMeanIntervalWidthFunction
    ));
    {
        CreateScalarFunctionInfo info(anofox_set);
        info.alias_of = "ts_mean_interval_width";
        FunctionDescription desc;
        desc.description = "Computes the mean width of prediction intervals: average of (upper - lower) across all steps.";
        desc.examples = {"ts_mean_interval_width(lower, upper)"};
        desc.categories = {"time-series", "uncertainty"};
        desc.parameter_names = {"lower", "upper"};
        desc.parameter_types = {
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))
        };
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

// ============================================================================
// ts_conformal_predict_per_step
// ts_conformal_predict_per_step(fold_forecasts[], fold_actuals[], point_forecasts[], alpha, horizon)
//   -> STRUCT(point[], lower[], upper[], half_widths[], coverage)
//
// fold_forecasts and fold_actuals are flat lists: [f1_h1, f1_h2, ..., f2_h1, f2_h2, ...]
// ============================================================================

static void TsConformalPredictPerStepFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &fold_fcst_vec = args.data[0];
    auto &fold_act_vec = args.data[1];
    auto &point_vec = args.data[2];
    auto &alpha_vec = args.data[3];
    auto &horizon_vec = args.data[4];
    idx_t count = args.size();

    ListDoubleReader fold_fcst_reader(fold_fcst_vec, count);
    ListDoubleReader fold_act_reader(fold_act_vec, count);
    ListDoubleReader point_reader(point_vec, count);
    ScalarArgReader<double> alpha_reader(alpha_vec, count);
    ScalarArgReader<int32_t> horizon_reader(horizon_vec, count);

    auto &struct_entries = StructVector::GetEntries(result);
    auto &point_out = *struct_entries[0];
    auto &lower_out = *struct_entries[1];
    auto &upper_out = *struct_entries[2];
    auto &hw_out = *struct_entries[3];
    auto &coverage_out = *struct_entries[4];

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (fold_fcst_reader.IsNull(row_idx) || fold_act_reader.IsNull(row_idx) ||
            point_reader.IsNull(row_idx) || alpha_reader.IsNull(row_idx) ||
            horizon_reader.IsNull(row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> fold_forecasts, fold_actuals, point_forecasts;
        vector<uint64_t> fold_forecasts_validity, fold_actuals_validity, point_validity;
        fold_fcst_reader.Read(row_idx, fold_forecasts, fold_forecasts_validity);
        fold_act_reader.Read(row_idx, fold_actuals, fold_actuals_validity);
        point_reader.Read(row_idx, point_forecasts, point_validity);
        double alpha = alpha_reader.Get(row_idx);
        int32_t horizon_raw = horizon_reader.Get(row_idx);
        // Rust owns the full validation (horizon > 0, equal lengths, length %
        // horizon == 0 — D-10); a non-positive horizon is folded into 0 here
        // so it hits the same "horizon must be positive" error uniformly.
        size_t horizon = horizon_raw > 0 ? static_cast<size_t>(horizon_raw) : 0;

        ConformalPerStepResultFFI ps_result = {};
        AnofoxError error;
        bool success = anofox_ts_conformal_predict_per_step(
            fold_forecasts.data(),
            fold_forecasts_validity.empty() ? nullptr : fold_forecasts_validity.data(),
            fold_forecasts.size(),
            fold_actuals.data(),
            fold_actuals_validity.empty() ? nullptr : fold_actuals_validity.data(),
            fold_actuals.size(),
            horizon,
            point_forecasts.data(),
            point_validity.empty() ? nullptr : point_validity.data(),
            point_forecasts.size(),
            alpha,
            &ps_result, &error
        );

        if (!success) {
            if (error.code == INVALID_INPUT) {
                throw InvalidInputException("ts_conformal_predict_per_step: %s", error.message);
            }
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        WriteDoubleListNanAsNull(point_out, row_idx, ps_result.point, ps_result.n_forecasts);
        WriteDoubleListNanAsNull(lower_out, row_idx, ps_result.lower, ps_result.n_forecasts);
        WriteDoubleListNanAsNull(upper_out, row_idx, ps_result.upper, ps_result.n_forecasts);
        WriteDoubleList(hw_out, row_idx, ps_result.half_widths, ps_result.n_forecasts);
        FlatVector::GetData<double>(coverage_out)[row_idx] = ps_result.coverage;

        anofox_free_conformal_per_step_result(&ps_result);
    }
}

void RegisterTsConformalPredictPerStepFunction(ExtensionLoader &loader) {
    child_list_t<LogicalType> struct_children;
    struct_children.push_back(make_pair("point", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    struct_children.push_back(make_pair("lower", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    struct_children.push_back(make_pair("upper", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    struct_children.push_back(make_pair("half_widths", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    struct_children.push_back(make_pair("coverage", LogicalType(LogicalTypeId::DOUBLE)));
    auto result_type = LogicalType::STRUCT(std::move(struct_children));

    // ts_conformal_predict_per_step(fold_forecasts[], fold_actuals[], point_forecasts[], alpha, horizon)
    ScalarFunctionSet ps_set("ts_conformal_predict_per_step");
    ps_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType(LogicalTypeId::DOUBLE),
         LogicalType(LogicalTypeId::INTEGER)},
        result_type,
        TsConformalPredictPerStepFunction
    ));
    {
        CreateScalarFunctionInfo info(ps_set);
        FunctionDescription desc;
        desc.description = "Per-step conformal prediction that calibrates separate intervals for each forecast horizon step.";
        desc.examples = {"ts_conformal_predict_per_step(calibration_actuals, calibration_forecasts, point_forecast, 0.1)"};
        desc.categories = {"time-series", "uncertainty", "conformal-prediction"};
        desc.parameter_names = {"fold_forecasts", "fold_actuals", "point_forecasts", "alpha", "horizon"};
        desc.parameter_types = {
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType(LogicalTypeId::DOUBLE),
            LogicalType(LogicalTypeId::INTEGER)
        };
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

    ScalarFunctionSet anofox_set("anofox_fcst_ts_conformal_predict_per_step");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType(LogicalTypeId::DOUBLE),
         LogicalType(LogicalTypeId::INTEGER)},
        result_type,
        TsConformalPredictPerStepFunction
    ));
    {
        CreateScalarFunctionInfo info(anofox_set);
        info.alias_of = "ts_conformal_predict_per_step";
        FunctionDescription desc;
        desc.description = "Per-step conformal prediction that calibrates separate intervals for each forecast horizon step.";
        desc.examples = {"ts_conformal_predict_per_step(calibration_actuals, calibration_forecasts, point_forecast, 0.1)"};
        desc.categories = {"time-series", "uncertainty", "conformal-prediction"};
        desc.parameter_names = {"fold_forecasts", "fold_actuals", "point_forecasts", "alpha", "horizon"};
        desc.parameter_types = {
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType(LogicalTypeId::DOUBLE),
            LogicalType(LogicalTypeId::INTEGER)
        };
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

// ============================================================================
// _ts_conformal_from_pairs / _ts_conformal_calibrate (Phase 16 D-01/D-02/D-03)
//
// Internal functions backing the ts_conformal_by / ts_conformal_calibrate
// macros: residuals are computed in Rust from per-row (actual, forecast)
// calibration pairs -- never subtracted in SQL -- and empirical coverage on
// the calibration set is reported alongside the requested nominal_level.
// ============================================================================

// Writes `value` into row `row_idx` of a DOUBLE result child, or SQL NULL if
// `value` is NaN (Phase 16: coverage/conformity_score can be NaN when there
// is no calibration data at all, OR when calibration is insufficient --
// D-05/G12).
static void WriteDoubleOrNull(Vector &vec, idx_t row_idx, double value) {
    if (std::isnan(value)) {
        FlatVector::SetNull(vec, row_idx, true);
    } else {
        FlatVector::GetData<double>(vec)[row_idx] = value;
    }
}

// _ts_conformal_from_pairs(cal_actuals[], cal_forecasts[], point_forecasts[], alpha, method)
//   -> STRUCT(point[], lower[], upper[], coverage, nominal_level,
//             conformity_score, n_calibration, method)
static void TsConformalFromPairsFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &cal_actuals_vec = args.data[0];
    auto &cal_forecasts_vec = args.data[1];
    auto &point_vec = args.data[2];
    auto &alpha_vec = args.data[3];
    auto &method_vec = args.data[4];
    idx_t count = args.size();

    ListDoubleReader cal_actuals_reader(cal_actuals_vec, count);
    ListDoubleReader cal_forecasts_reader(cal_forecasts_vec, count);
    ListDoubleReader point_reader(point_vec, count);
    ScalarArgReader<double> alpha_reader(alpha_vec, count);
    ScalarArgReader<string_t> method_reader(method_vec, count);

    auto &struct_entries = StructVector::GetEntries(result);
    auto &point_out = *struct_entries[0];
    auto &lower_out = *struct_entries[1];
    auto &upper_out = *struct_entries[2];
    auto &coverage_out = *struct_entries[3];
    auto &nominal_level_out = *struct_entries[4];
    auto &score_out = *struct_entries[5];
    auto &n_calibration_out = *struct_entries[6];
    auto &method_out = *struct_entries[7];
    auto &calibration_insufficient_out = *struct_entries[8];

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (cal_actuals_reader.IsNull(row_idx) || cal_forecasts_reader.IsNull(row_idx) ||
            point_reader.IsNull(row_idx) || alpha_reader.IsNull(row_idx) ||
            method_reader.IsNull(row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> cal_actuals, cal_forecasts, point_forecasts;
        vector<uint64_t> cal_actuals_validity, cal_forecasts_validity, point_validity;
        cal_actuals_reader.Read(row_idx, cal_actuals, cal_actuals_validity);
        cal_forecasts_reader.Read(row_idx, cal_forecasts, cal_forecasts_validity);
        point_reader.Read(row_idx, point_forecasts, point_validity);
        double alpha = alpha_reader.Get(row_idx);
        auto method_str = method_reader.Get(row_idx).GetString();

        ConformalResultFFI conf_result = {};
        AnofoxError error;
        bool success = anofox_ts_conformal_from_pairs(
            cal_actuals.data(), cal_actuals_validity.empty() ? nullptr : cal_actuals_validity.data(),
            cal_actuals.size(),
            cal_forecasts.data(), cal_forecasts_validity.empty() ? nullptr : cal_forecasts_validity.data(),
            cal_forecasts.size(),
            point_forecasts.data(), point_validity.empty() ? nullptr : point_validity.data(),
            point_forecasts.size(),
            alpha, method_str.c_str(),
            &conf_result, &error
        );

        if (!success) {
            if (error.code == INVALID_INPUT) {
                throw InvalidInputException("_ts_conformal_from_pairs: %s", error.message);
            }
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        WriteDoubleListNanAsNull(point_out, row_idx, conf_result.point, conf_result.n_forecasts);
        WriteDoubleListNanAsNull(lower_out, row_idx, conf_result.lower, conf_result.n_forecasts);
        WriteDoubleListNanAsNull(upper_out, row_idx, conf_result.upper, conf_result.n_forecasts);
        WriteDoubleOrNull(coverage_out, row_idx, conf_result.coverage);
        FlatVector::GetData<double>(nominal_level_out)[row_idx] = conf_result.nominal_level;
        WriteDoubleOrNull(score_out, row_idx, conf_result.conformity_score);
        FlatVector::GetData<int64_t>(n_calibration_out)[row_idx] = (int64_t)conf_result.n_calibration;
        FlatVector::GetData<string_t>(method_out)[row_idx] = StringVector::AddString(method_out, conf_result.method);
        FlatVector::GetData<bool>(calibration_insufficient_out)[row_idx] = conf_result.calibration_insufficient;

        anofox_free_conformal_result(&conf_result);
    }
}

// _ts_conformal_calibrate(actuals[], forecasts[], alpha, method)
//   -> STRUCT(method, conformity_score, coverage, nominal_level, n_calibration)
static void TsConformalCalibrateFromPairsFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &actuals_vec = args.data[0];
    auto &forecasts_vec = args.data[1];
    auto &alpha_vec = args.data[2];
    auto &method_vec = args.data[3];
    idx_t count = args.size();

    ListDoubleReader actuals_reader(actuals_vec, count);
    ListDoubleReader forecasts_reader(forecasts_vec, count);
    ScalarArgReader<double> alpha_reader(alpha_vec, count);
    ScalarArgReader<string_t> method_reader(method_vec, count);

    auto &struct_entries = StructVector::GetEntries(result);
    auto &method_out = *struct_entries[0];
    auto &score_out = *struct_entries[1];
    auto &coverage_out = *struct_entries[2];
    auto &nominal_level_out = *struct_entries[3];
    auto &n_calibration_out = *struct_entries[4];
    auto &calibration_insufficient_out = *struct_entries[5];

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (actuals_reader.IsNull(row_idx) || forecasts_reader.IsNull(row_idx) ||
            alpha_reader.IsNull(row_idx) || method_reader.IsNull(row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> actuals, forecasts;
        vector<uint64_t> actuals_validity, forecasts_validity;
        actuals_reader.Read(row_idx, actuals, actuals_validity);
        forecasts_reader.Read(row_idx, forecasts, forecasts_validity);
        double alpha = alpha_reader.Get(row_idx);
        auto method_str = method_reader.Get(row_idx).GetString();

        ConformalCalibrationFFI cal_result = {};
        AnofoxError error;
        bool success = anofox_ts_conformal_calibrate(
            actuals.data(), actuals_validity.empty() ? nullptr : actuals_validity.data(),
            actuals.size(),
            forecasts.data(), forecasts_validity.empty() ? nullptr : forecasts_validity.data(),
            forecasts.size(),
            alpha, method_str.c_str(),
            &cal_result, &error
        );

        if (!success) {
            if (error.code == INVALID_INPUT) {
                throw InvalidInputException("_ts_conformal_calibrate: %s", error.message);
            }
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        FlatVector::GetData<string_t>(method_out)[row_idx] = StringVector::AddString(method_out, cal_result.method);
        WriteDoubleOrNull(score_out, row_idx, cal_result.conformity_score);
        WriteDoubleOrNull(coverage_out, row_idx, cal_result.coverage);
        FlatVector::GetData<double>(nominal_level_out)[row_idx] = cal_result.nominal_level;
        FlatVector::GetData<int64_t>(n_calibration_out)[row_idx] = (int64_t)cal_result.n_calibration;
        FlatVector::GetData<bool>(calibration_insufficient_out)[row_idx] = cal_result.calibration_insufficient;
    }
}

void RegisterTsConformalFromPairsFunction(ExtensionLoader &loader) {
    {
        child_list_t<LogicalType> struct_children;
        struct_children.push_back(make_pair("point", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
        struct_children.push_back(make_pair("lower", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
        struct_children.push_back(make_pair("upper", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
        struct_children.push_back(make_pair("coverage", LogicalType(LogicalTypeId::DOUBLE)));
        struct_children.push_back(make_pair("nominal_level", LogicalType(LogicalTypeId::DOUBLE)));
        struct_children.push_back(make_pair("conformity_score", LogicalType(LogicalTypeId::DOUBLE)));
        struct_children.push_back(make_pair("n_calibration", LogicalType(LogicalTypeId::BIGINT)));
        struct_children.push_back(make_pair("method", LogicalType(LogicalTypeId::VARCHAR)));
        struct_children.push_back(make_pair("calibration_insufficient", LogicalType(LogicalTypeId::BOOLEAN)));
        auto result_type = LogicalType::STRUCT(std::move(struct_children));

        ScalarFunction from_pairs_func(
            "_ts_conformal_from_pairs",
            {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
             LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
             LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
             LogicalType(LogicalTypeId::DOUBLE),
             LogicalType(LogicalTypeId::VARCHAR)},
            result_type,
            TsConformalFromPairsFunction
        );
        CreateScalarFunctionInfo info(from_pairs_func);
        info.internal = true;
        loader.RegisterFunction(info);
    }

    {
        child_list_t<LogicalType> struct_children;
        struct_children.push_back(make_pair("method", LogicalType(LogicalTypeId::VARCHAR)));
        struct_children.push_back(make_pair("conformity_score", LogicalType(LogicalTypeId::DOUBLE)));
        struct_children.push_back(make_pair("coverage", LogicalType(LogicalTypeId::DOUBLE)));
        struct_children.push_back(make_pair("nominal_level", LogicalType(LogicalTypeId::DOUBLE)));
        struct_children.push_back(make_pair("n_calibration", LogicalType(LogicalTypeId::BIGINT)));
        struct_children.push_back(make_pair("calibration_insufficient", LogicalType(LogicalTypeId::BOOLEAN)));
        auto result_type = LogicalType::STRUCT(std::move(struct_children));

        ScalarFunction calibrate_func(
            "_ts_conformal_calibrate",
            {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
             LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
             LogicalType(LogicalTypeId::DOUBLE),
             LogicalType(LogicalTypeId::VARCHAR)},
            result_type,
            TsConformalCalibrateFromPairsFunction
        );
        CreateScalarFunctionInfo info(calibrate_func);
        info.internal = true;
        loader.RegisterFunction(info);
    }
}

} // namespace duckdb
