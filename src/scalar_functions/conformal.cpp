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
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE},
        LogicalType::DOUBLE,
        TsConformalQuantileFunction
    ));
    loader.RegisterFunction(ts_cq_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_conformal_quantile");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE},
        LogicalType::DOUBLE,
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
    struct_children.push_back(make_pair("lower", LogicalType::LIST(LogicalType::DOUBLE)));
    struct_children.push_back(make_pair("upper", LogicalType::LIST(LogicalType::DOUBLE)));
    auto result_type = LogicalType::STRUCT(std::move(struct_children));

    ScalarFunctionSet ts_ci_set("ts_conformal_intervals");
    ts_ci_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE},
        result_type,
        TsConformalIntervalsFunction
    ));
    loader.RegisterFunction(ts_ci_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_conformal_intervals");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE},
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
    struct_children.push_back(make_pair("point", LogicalType::LIST(LogicalType::DOUBLE)));
    struct_children.push_back(make_pair("lower", LogicalType::LIST(LogicalType::DOUBLE)));
    struct_children.push_back(make_pair("upper", LogicalType::LIST(LogicalType::DOUBLE)));
    struct_children.push_back(make_pair("coverage", LogicalType::DOUBLE));
    struct_children.push_back(make_pair("conformity_score", LogicalType::DOUBLE));
    struct_children.push_back(make_pair("method", LogicalType::VARCHAR));
    auto result_type = LogicalType::STRUCT(std::move(struct_children));

    ScalarFunctionSet ts_cp_set("ts_conformal_predict");
    ts_cp_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE},
        result_type,
        TsConformalPredictFunction
    ));
    loader.RegisterFunction(ts_cp_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_conformal_predict");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE},
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
    struct_children.push_back(make_pair("point", LogicalType::LIST(LogicalType::DOUBLE)));
    struct_children.push_back(make_pair("lower", LogicalType::LIST(LogicalType::DOUBLE)));
    struct_children.push_back(make_pair("upper", LogicalType::LIST(LogicalType::DOUBLE)));
    struct_children.push_back(make_pair("coverage", LogicalType::DOUBLE));
    struct_children.push_back(make_pair("conformity_score", LogicalType::DOUBLE));
    struct_children.push_back(make_pair("method", LogicalType::VARCHAR));
    auto result_type = LogicalType::STRUCT(std::move(struct_children));

    ScalarFunctionSet ts_cpa_set("ts_conformal_predict_asymmetric");
    ts_cpa_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE},
        result_type,
        TsConformalPredictAsymmetricFunction
    ));
    loader.RegisterFunction(ts_cpa_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_conformal_predict_asymmetric");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE},
        result_type,
        TsConformalPredictAsymmetricFunction
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
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::DOUBLE,
        TsMeanIntervalWidthFunction
    ));
    loader.RegisterFunction(ts_miw_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_mean_interval_width");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::DOUBLE,
        TsMeanIntervalWidthFunction
    ));
    loader.RegisterFunction(anofox_set);
}

} // namespace duckdb
