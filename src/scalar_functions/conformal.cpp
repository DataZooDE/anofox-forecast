#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"

#include "duckdb/function/scalar_function.hpp"
#include "duckdb/common/vector_operations/generic_executor.hpp"

namespace duckdb {

// Helper to extract values from LIST vectors
static void ExtractListAsDouble(Vector &list_vec, idx_t row_idx, vector<double> &out_values) {
    auto list_data = ListVector::GetData(list_vec);
    auto &list_entry = list_data[row_idx];

    auto &child_vec = ListVector::GetEntry(list_vec);
    auto child_data = FlatVector::GetData<double>(child_vec);
    auto &child_validity = FlatVector::Validity(child_vec);

    out_values.clear();
    out_values.reserve(list_entry.length);

    for (idx_t i = 0; i < list_entry.length; i++) {
        idx_t child_idx = list_entry.offset + i;
        if (child_validity.RowIsValid(child_idx)) {
            out_values.push_back(child_data[child_idx]);
        }
    }
}

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

    UnifiedVectorFormat alpha_data;
    alpha_vec.ToUnifiedFormat(count, alpha_data);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        auto alpha_idx = alpha_data.sel->get_index(row_idx);
        if (FlatVector::IsNull(residuals_vec, row_idx) || !alpha_data.validity.RowIsValid(alpha_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> residuals;
        ExtractListAsDouble(residuals_vec, row_idx, residuals);
        double alpha = UnifiedVectorFormat::GetData<double>(alpha_data)[alpha_idx];

        AnofoxError error;
        double quantile_result;
        bool success = anofox_ts_conformal_quantile(
            residuals.data(), nullptr, residuals.size(),
            alpha, &quantile_result, &error
        );

        if (!success) {
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
    loader.RegisterFunction(ts_cq_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_conformal_quantile");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE)},
        LogicalType(LogicalTypeId::DOUBLE),
        TsConformalQuantileFunction
    ));
    loader.RegisterFunction(anofox_set);
}

// ============================================================================
// ts_conformal_intervals - Apply conformity score to create intervals
// ts_conformal_intervals(forecasts[], conformity_score) -> STRUCT(lower[], upper[])
// ============================================================================

static void TsConformalIntervalsFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &forecasts_vec = args.data[0];
    auto &score_vec = args.data[1];
    idx_t count = args.size();

    UnifiedVectorFormat score_data;
    score_vec.ToUnifiedFormat(count, score_data);

    // Result is a STRUCT with lower and upper arrays
    auto &struct_entries = StructVector::GetEntries(result);
    auto &lower_vec = *struct_entries[0];
    auto &upper_vec = *struct_entries[1];

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        auto score_idx = score_data.sel->get_index(row_idx);
        if (FlatVector::IsNull(forecasts_vec, row_idx) || !score_data.validity.RowIsValid(score_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> forecasts;
        ExtractListAsDouble(forecasts_vec, row_idx, forecasts);
        double conformity_score = UnifiedVectorFormat::GetData<double>(score_data)[score_idx];

        AnofoxError error;
        double *out_lower = nullptr;
        double *out_upper = nullptr;
        bool success = anofox_ts_conformal_intervals(
            forecasts.data(), forecasts.size(), conformity_score,
            &out_lower, &out_upper, &error
        );

        if (!success || out_lower == nullptr || out_upper == nullptr) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // Build lower list
        auto lower_offset = ListVector::GetListSize(lower_vec);
        auto &lower_child = ListVector::GetEntry(lower_vec);
        ListVector::Reserve(lower_vec, lower_offset + forecasts.size());
        auto lower_child_data = FlatVector::GetData<double>(lower_child);
        for (idx_t i = 0; i < forecasts.size(); i++) {
            lower_child_data[lower_offset + i] = out_lower[i];
        }
        ListVector::SetListSize(lower_vec, lower_offset + forecasts.size());
        auto lower_list_data = ListVector::GetData(lower_vec);
        lower_list_data[row_idx].offset = lower_offset;
        lower_list_data[row_idx].length = forecasts.size();

        // Build upper list
        auto upper_offset = ListVector::GetListSize(upper_vec);
        auto &upper_child = ListVector::GetEntry(upper_vec);
        ListVector::Reserve(upper_vec, upper_offset + forecasts.size());
        auto upper_child_data = FlatVector::GetData<double>(upper_child);
        for (idx_t i = 0; i < forecasts.size(); i++) {
            upper_child_data[upper_offset + i] = out_upper[i];
        }
        ListVector::SetListSize(upper_vec, upper_offset + forecasts.size());
        auto upper_list_data = ListVector::GetData(upper_vec);
        upper_list_data[row_idx].offset = upper_offset;
        upper_list_data[row_idx].length = forecasts.size();

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
    loader.RegisterFunction(ts_ci_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_conformal_intervals");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE)},
        result_type,
        TsConformalIntervalsFunction
    ));
    loader.RegisterFunction(anofox_set);
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

    UnifiedVectorFormat alpha_data;
    alpha_vec.ToUnifiedFormat(count, alpha_data);

    // Result struct fields
    auto &struct_entries = StructVector::GetEntries(result);
    auto &point_vec = *struct_entries[0];
    auto &lower_vec = *struct_entries[1];
    auto &upper_vec = *struct_entries[2];
    auto &coverage_vec = *struct_entries[3];
    auto &score_vec = *struct_entries[4];
    auto &method_vec = *struct_entries[5];

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        auto alpha_idx = alpha_data.sel->get_index(row_idx);
        if (FlatVector::IsNull(residuals_vec, row_idx) ||
            FlatVector::IsNull(forecasts_vec, row_idx) ||
            !alpha_data.validity.RowIsValid(alpha_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> residuals, forecasts;
        ExtractListAsDouble(residuals_vec, row_idx, residuals);
        ExtractListAsDouble(forecasts_vec, row_idx, forecasts);
        double alpha = UnifiedVectorFormat::GetData<double>(alpha_data)[alpha_idx];

        ConformalResultFFI conf_result = {};
        AnofoxError error;
        bool success = anofox_ts_conformal_predict(
            residuals.data(), nullptr, residuals.size(),
            forecasts.data(), forecasts.size(),
            alpha, &conf_result, &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // Build point list
        auto point_offset = ListVector::GetListSize(point_vec);
        auto &point_child = ListVector::GetEntry(point_vec);
        ListVector::Reserve(point_vec, point_offset + conf_result.n_forecasts);
        auto point_child_data = FlatVector::GetData<double>(point_child);
        for (idx_t i = 0; i < conf_result.n_forecasts; i++) {
            point_child_data[point_offset + i] = conf_result.point[i];
        }
        ListVector::SetListSize(point_vec, point_offset + conf_result.n_forecasts);
        auto point_list_data = ListVector::GetData(point_vec);
        point_list_data[row_idx].offset = point_offset;
        point_list_data[row_idx].length = conf_result.n_forecasts;

        // Build lower list
        auto lower_offset = ListVector::GetListSize(lower_vec);
        auto &lower_child = ListVector::GetEntry(lower_vec);
        ListVector::Reserve(lower_vec, lower_offset + conf_result.n_forecasts);
        auto lower_child_data = FlatVector::GetData<double>(lower_child);
        for (idx_t i = 0; i < conf_result.n_forecasts; i++) {
            lower_child_data[lower_offset + i] = conf_result.lower[i];
        }
        ListVector::SetListSize(lower_vec, lower_offset + conf_result.n_forecasts);
        auto lower_list_data = ListVector::GetData(lower_vec);
        lower_list_data[row_idx].offset = lower_offset;
        lower_list_data[row_idx].length = conf_result.n_forecasts;

        // Build upper list
        auto upper_offset = ListVector::GetListSize(upper_vec);
        auto &upper_child = ListVector::GetEntry(upper_vec);
        ListVector::Reserve(upper_vec, upper_offset + conf_result.n_forecasts);
        auto upper_child_data = FlatVector::GetData<double>(upper_child);
        for (idx_t i = 0; i < conf_result.n_forecasts; i++) {
            upper_child_data[upper_offset + i] = conf_result.upper[i];
        }
        ListVector::SetListSize(upper_vec, upper_offset + conf_result.n_forecasts);
        auto upper_list_data = ListVector::GetData(upper_vec);
        upper_list_data[row_idx].offset = upper_offset;
        upper_list_data[row_idx].length = conf_result.n_forecasts;

        // Scalar fields
        FlatVector::GetData<double>(coverage_vec)[row_idx] = conf_result.coverage;
        FlatVector::GetData<double>(score_vec)[row_idx] = conf_result.conformity_score;
        FlatVector::GetData<string_t>(method_vec)[row_idx] = StringVector::AddString(method_vec, conf_result.method);

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
    auto result_type = LogicalType::STRUCT(std::move(struct_children));

    ScalarFunctionSet ts_cp_set("ts_conformal_predict");
    ts_cp_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE)},
        result_type,
        TsConformalPredictFunction
    ));
    loader.RegisterFunction(ts_cp_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_conformal_predict");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE)},
        result_type,
        TsConformalPredictFunction
    ));
    loader.RegisterFunction(anofox_set);
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

    UnifiedVectorFormat alpha_data;
    alpha_vec.ToUnifiedFormat(count, alpha_data);

    // Result struct fields
    auto &struct_entries = StructVector::GetEntries(result);
    auto &point_vec = *struct_entries[0];
    auto &lower_vec = *struct_entries[1];
    auto &upper_vec = *struct_entries[2];
    auto &coverage_vec = *struct_entries[3];
    auto &score_vec = *struct_entries[4];
    auto &method_vec = *struct_entries[5];

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        auto alpha_idx = alpha_data.sel->get_index(row_idx);
        if (FlatVector::IsNull(residuals_vec, row_idx) ||
            FlatVector::IsNull(forecasts_vec, row_idx) ||
            !alpha_data.validity.RowIsValid(alpha_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> residuals, forecasts;
        ExtractListAsDouble(residuals_vec, row_idx, residuals);
        ExtractListAsDouble(forecasts_vec, row_idx, forecasts);
        double alpha = UnifiedVectorFormat::GetData<double>(alpha_data)[alpha_idx];

        ConformalResultFFI conf_result = {};
        AnofoxError error;
        bool success = anofox_ts_conformal_predict_asymmetric(
            residuals.data(), nullptr, residuals.size(),
            forecasts.data(), forecasts.size(),
            alpha, &conf_result, &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // Build point list
        auto point_offset = ListVector::GetListSize(point_vec);
        auto &point_child = ListVector::GetEntry(point_vec);
        ListVector::Reserve(point_vec, point_offset + conf_result.n_forecasts);
        auto point_child_data = FlatVector::GetData<double>(point_child);
        for (idx_t i = 0; i < conf_result.n_forecasts; i++) {
            point_child_data[point_offset + i] = conf_result.point[i];
        }
        ListVector::SetListSize(point_vec, point_offset + conf_result.n_forecasts);
        auto point_list_data = ListVector::GetData(point_vec);
        point_list_data[row_idx].offset = point_offset;
        point_list_data[row_idx].length = conf_result.n_forecasts;

        // Build lower list
        auto lower_offset = ListVector::GetListSize(lower_vec);
        auto &lower_child = ListVector::GetEntry(lower_vec);
        ListVector::Reserve(lower_vec, lower_offset + conf_result.n_forecasts);
        auto lower_child_data = FlatVector::GetData<double>(lower_child);
        for (idx_t i = 0; i < conf_result.n_forecasts; i++) {
            lower_child_data[lower_offset + i] = conf_result.lower[i];
        }
        ListVector::SetListSize(lower_vec, lower_offset + conf_result.n_forecasts);
        auto lower_list_data = ListVector::GetData(lower_vec);
        lower_list_data[row_idx].offset = lower_offset;
        lower_list_data[row_idx].length = conf_result.n_forecasts;

        // Build upper list
        auto upper_offset = ListVector::GetListSize(upper_vec);
        auto &upper_child = ListVector::GetEntry(upper_vec);
        ListVector::Reserve(upper_vec, upper_offset + conf_result.n_forecasts);
        auto upper_child_data = FlatVector::GetData<double>(upper_child);
        for (idx_t i = 0; i < conf_result.n_forecasts; i++) {
            upper_child_data[upper_offset + i] = conf_result.upper[i];
        }
        ListVector::SetListSize(upper_vec, upper_offset + conf_result.n_forecasts);
        auto upper_list_data = ListVector::GetData(upper_vec);
        upper_list_data[row_idx].offset = upper_offset;
        upper_list_data[row_idx].length = conf_result.n_forecasts;

        // Scalar fields
        FlatVector::GetData<double>(coverage_vec)[row_idx] = conf_result.coverage;
        FlatVector::GetData<double>(score_vec)[row_idx] = conf_result.conformity_score;
        FlatVector::GetData<string_t>(method_vec)[row_idx] = StringVector::AddString(method_vec, conf_result.method);

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
    auto result_type = LogicalType::STRUCT(std::move(struct_children));

    ScalarFunctionSet ts_cpa_set("ts_conformal_predict_asymmetric");
    ts_cpa_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE)},
        result_type,
        TsConformalPredictAsymmetricFunction
    ));
    loader.RegisterFunction(ts_cpa_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_conformal_predict_asymmetric");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE)},
        result_type,
        TsConformalPredictAsymmetricFunction
    ));
    loader.RegisterFunction(anofox_set);
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

    UnifiedVectorFormat method_data, strategy_data;
    method_vec.ToUnifiedFormat(count, method_data);
    strategy_vec.ToUnifiedFormat(count, strategy_data);

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
        auto method_idx = method_data.sel->get_index(row_idx);
        auto strategy_idx = strategy_data.sel->get_index(row_idx);

        if (FlatVector::IsNull(residuals_vec, row_idx) ||
            FlatVector::IsNull(alphas_vec, row_idx) ||
            !method_data.validity.RowIsValid(method_idx) ||
            !strategy_data.validity.RowIsValid(strategy_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> residuals, alphas;
        ExtractListAsDouble(residuals_vec, row_idx, residuals);
        ExtractListAsDouble(alphas_vec, row_idx, alphas);

        auto method_str = UnifiedVectorFormat::GetData<string_t>(method_data)[method_idx].GetString();
        auto strategy_str = UnifiedVectorFormat::GetData<string_t>(strategy_data)[strategy_idx].GetString();

        ConformalMethodFFI method = ParseConformalMethod(method_str);
        ConformalStrategyFFI strategy = ParseConformalStrategy(strategy_str);

        CalibrationProfileFFI profile = {};
        AnofoxError error;
        bool success = anofox_ts_conformal_learn(
            residuals.data(), nullptr, residuals.size(),
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

        // Build alphas list
        auto alphas_offset = ListVector::GetListSize(out_alphas_vec);
        auto &alphas_child = ListVector::GetEntry(out_alphas_vec);
        ListVector::Reserve(out_alphas_vec, alphas_offset + profile.n_levels);
        auto alphas_child_data = FlatVector::GetData<double>(alphas_child);
        for (idx_t i = 0; i < profile.n_levels; i++) {
            alphas_child_data[alphas_offset + i] = profile.alphas[i];
        }
        ListVector::SetListSize(out_alphas_vec, alphas_offset + profile.n_levels);
        auto alphas_list_data = ListVector::GetData(out_alphas_vec);
        alphas_list_data[row_idx].offset = alphas_offset;
        alphas_list_data[row_idx].length = profile.n_levels;

        // Build state_vector list
        auto state_offset = ListVector::GetListSize(out_state_vec);
        auto &state_child = ListVector::GetEntry(out_state_vec);
        ListVector::Reserve(out_state_vec, state_offset + profile.state_vector_len);
        auto state_child_data = FlatVector::GetData<double>(state_child);
        for (idx_t i = 0; i < profile.state_vector_len; i++) {
            state_child_data[state_offset + i] = profile.state_vector[i];
        }
        ListVector::SetListSize(out_state_vec, state_offset + profile.state_vector_len);
        auto state_list_data = ListVector::GetData(out_state_vec);
        state_list_data[row_idx].offset = state_offset;
        state_list_data[row_idx].length = profile.state_vector_len;

        // Build scores_lower list
        auto lower_offset = ListVector::GetListSize(out_scores_lower_vec);
        auto &lower_child = ListVector::GetEntry(out_scores_lower_vec);
        ListVector::Reserve(out_scores_lower_vec, lower_offset + profile.n_levels);
        auto lower_child_data = FlatVector::GetData<double>(lower_child);
        for (idx_t i = 0; i < profile.n_levels; i++) {
            lower_child_data[lower_offset + i] = profile.scores_lower[i];
        }
        ListVector::SetListSize(out_scores_lower_vec, lower_offset + profile.n_levels);
        auto lower_list_data = ListVector::GetData(out_scores_lower_vec);
        lower_list_data[row_idx].offset = lower_offset;
        lower_list_data[row_idx].length = profile.n_levels;

        // Build scores_upper list
        auto upper_offset = ListVector::GetListSize(out_scores_upper_vec);
        auto &upper_child = ListVector::GetEntry(out_scores_upper_vec);
        ListVector::Reserve(out_scores_upper_vec, upper_offset + profile.n_levels);
        auto upper_child_data = FlatVector::GetData<double>(upper_child);
        for (idx_t i = 0; i < profile.n_levels; i++) {
            upper_child_data[upper_offset + i] = profile.scores_upper[i];
        }
        ListVector::SetListSize(out_scores_upper_vec, upper_offset + profile.n_levels);
        auto upper_list_data = ListVector::GetData(out_scores_upper_vec);
        upper_list_data[row_idx].offset = upper_offset;
        upper_list_data[row_idx].length = profile.n_levels;

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
    loader.RegisterFunction(ts_cl_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_conformal_learn");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::VARCHAR), LogicalType(LogicalTypeId::VARCHAR)},
        result_type,
        TsConformalLearnFunction
    ));
    loader.RegisterFunction(anofox_set);
}

// ============================================================================
// ts_conformal_apply - Apply a calibration profile to forecasts
// ts_conformal_apply(forecasts[], profile_struct) -> STRUCT
// ============================================================================

static void TsConformalApplyFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &forecasts_vec = args.data[0];
    auto &profile_vec = args.data[1];
    idx_t count = args.size();

    // Profile struct fields
    auto &profile_entries = StructVector::GetEntries(profile_vec);
    auto &method_vec = *profile_entries[0];
    auto &strategy_vec_input = *profile_entries[1];
    auto &alphas_vec = *profile_entries[2];
    auto &state_vec = *profile_entries[3];
    auto &scores_lower_vec = *profile_entries[4];
    auto &scores_upper_vec = *profile_entries[5];
    auto &n_residuals_vec = *profile_entries[6];

    // Result struct fields
    auto &out_entries = StructVector::GetEntries(result);
    auto &out_point_vec = *out_entries[0];
    auto &out_coverage_vec = *out_entries[1];
    auto &out_lower_vec = *out_entries[2];
    auto &out_upper_vec = *out_entries[3];
    auto &out_method_vec = *out_entries[4];

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(forecasts_vec, row_idx) || FlatVector::IsNull(profile_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> forecasts;
        ExtractListAsDouble(forecasts_vec, row_idx, forecasts);

        // Extract profile fields
        auto method_str = FlatVector::GetData<string_t>(method_vec)[row_idx].GetString();
        auto strategy_str = FlatVector::GetData<string_t>(strategy_vec_input)[row_idx].GetString();
        auto n_residuals = FlatVector::GetData<int64_t>(n_residuals_vec)[row_idx];

        vector<double> alphas, state_vector, scores_lower, scores_upper;
        ExtractListAsDouble(alphas_vec, row_idx, alphas);
        ExtractListAsDouble(state_vec, row_idx, state_vector);
        ExtractListAsDouble(scores_lower_vec, row_idx, scores_lower);
        ExtractListAsDouble(scores_upper_vec, row_idx, scores_upper);

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
            forecasts.data(), forecasts.size(),
            &profile,
            nullptr,  // no difficulty scores
            &intervals, &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // Build point list
        auto point_offset = ListVector::GetListSize(out_point_vec);
        auto &point_child = ListVector::GetEntry(out_point_vec);
        ListVector::Reserve(out_point_vec, point_offset + intervals.n_forecasts);
        auto point_child_data = FlatVector::GetData<double>(point_child);
        for (idx_t i = 0; i < intervals.n_forecasts; i++) {
            point_child_data[point_offset + i] = intervals.point[i];
        }
        ListVector::SetListSize(out_point_vec, point_offset + intervals.n_forecasts);
        auto point_list_data = ListVector::GetData(out_point_vec);
        point_list_data[row_idx].offset = point_offset;
        point_list_data[row_idx].length = intervals.n_forecasts;

        // Build coverage list
        auto cov_offset = ListVector::GetListSize(out_coverage_vec);
        auto &cov_child = ListVector::GetEntry(out_coverage_vec);
        ListVector::Reserve(out_coverage_vec, cov_offset + intervals.n_levels);
        auto cov_child_data = FlatVector::GetData<double>(cov_child);
        for (idx_t i = 0; i < intervals.n_levels; i++) {
            cov_child_data[cov_offset + i] = intervals.coverage[i];
        }
        ListVector::SetListSize(out_coverage_vec, cov_offset + intervals.n_levels);
        auto cov_list_data = ListVector::GetData(out_coverage_vec);
        cov_list_data[row_idx].offset = cov_offset;
        cov_list_data[row_idx].length = intervals.n_levels;

        // Build lower list (flattened: n_levels * n_forecasts)
        size_t total_lower = intervals.n_levels * intervals.n_forecasts;
        auto lower_offset = ListVector::GetListSize(out_lower_vec);
        auto &lower_child = ListVector::GetEntry(out_lower_vec);
        ListVector::Reserve(out_lower_vec, lower_offset + total_lower);
        auto lower_child_data = FlatVector::GetData<double>(lower_child);
        for (idx_t i = 0; i < total_lower; i++) {
            lower_child_data[lower_offset + i] = intervals.lower[i];
        }
        ListVector::SetListSize(out_lower_vec, lower_offset + total_lower);
        auto lower_list_data = ListVector::GetData(out_lower_vec);
        lower_list_data[row_idx].offset = lower_offset;
        lower_list_data[row_idx].length = total_lower;

        // Build upper list (flattened: n_levels * n_forecasts)
        size_t total_upper = intervals.n_levels * intervals.n_forecasts;
        auto upper_offset = ListVector::GetListSize(out_upper_vec);
        auto &upper_child = ListVector::GetEntry(out_upper_vec);
        ListVector::Reserve(out_upper_vec, upper_offset + total_upper);
        auto upper_child_data = FlatVector::GetData<double>(upper_child);
        for (idx_t i = 0; i < total_upper; i++) {
            upper_child_data[upper_offset + i] = intervals.upper[i];
        }
        ListVector::SetListSize(out_upper_vec, upper_offset + total_upper);
        auto upper_list_data = ListVector::GetData(out_upper_vec);
        upper_list_data[row_idx].offset = upper_offset;
        upper_list_data[row_idx].length = total_upper;

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
    loader.RegisterFunction(ts_ca_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_conformal_apply");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), profile_type},
        result_type,
        TsConformalApplyFunction
    ));
    loader.RegisterFunction(anofox_set);
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

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(actuals_vec, row_idx) ||
            FlatVector::IsNull(lower_vec, row_idx) ||
            FlatVector::IsNull(upper_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> actuals, lower, upper;
        ExtractListAsDouble(actuals_vec, row_idx, actuals);
        ExtractListAsDouble(lower_vec, row_idx, lower);
        ExtractListAsDouble(upper_vec, row_idx, upper);

        if (actuals.size() != lower.size() || actuals.size() != upper.size()) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        AnofoxError error;
        double coverage_result;
        bool success = anofox_ts_conformal_coverage(
            actuals.data(), lower.data(), upper.data(), actuals.size(),
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
    loader.RegisterFunction(ts_cc_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_conformal_coverage");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType(LogicalTypeId::DOUBLE),
        TsConformalCoverageFunction
    ));
    loader.RegisterFunction(anofox_set);
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

    UnifiedVectorFormat alpha_data;
    alpha_vec.ToUnifiedFormat(count, alpha_data);

    // Result struct fields
    auto &struct_entries = StructVector::GetEntries(result);
    auto &out_coverage_vec = *struct_entries[0];
    auto &out_violation_vec = *struct_entries[1];
    auto &out_width_vec = *struct_entries[2];
    auto &out_winkler_vec = *struct_entries[3];
    auto &out_n_obs_vec = *struct_entries[4];

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        auto alpha_idx = alpha_data.sel->get_index(row_idx);

        if (FlatVector::IsNull(actuals_vec, row_idx) ||
            FlatVector::IsNull(lower_vec, row_idx) ||
            FlatVector::IsNull(upper_vec, row_idx) ||
            !alpha_data.validity.RowIsValid(alpha_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> actuals, lower, upper;
        ExtractListAsDouble(actuals_vec, row_idx, actuals);
        ExtractListAsDouble(lower_vec, row_idx, lower);
        ExtractListAsDouble(upper_vec, row_idx, upper);
        double alpha = UnifiedVectorFormat::GetData<double>(alpha_data)[alpha_idx];

        if (actuals.size() != lower.size() || actuals.size() != upper.size()) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        ConformalEvaluationFFI eval = {};
        AnofoxError error;
        bool success = anofox_ts_conformal_evaluate(
            actuals.data(), lower.data(), upper.data(), actuals.size(),
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
    loader.RegisterFunction(ts_ce_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_conformal_evaluate");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE)},
        result_type,
        TsConformalEvaluateFunction
    ));
    loader.RegisterFunction(anofox_set);
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

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(lower_vec, row_idx) || FlatVector::IsNull(upper_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> lower, upper;
        ExtractListAsDouble(lower_vec, row_idx, lower);
        ExtractListAsDouble(upper_vec, row_idx, upper);

        if (lower.size() != upper.size()) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        AnofoxError error;
        double miw_result;
        bool success = anofox_ts_mean_interval_width(
            lower.data(), upper.data(), lower.size(),
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
    loader.RegisterFunction(ts_miw_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_mean_interval_width");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType(LogicalTypeId::DOUBLE),
        TsMeanIntervalWidthFunction
    ));
    loader.RegisterFunction(anofox_set);
}

} // namespace duckdb
