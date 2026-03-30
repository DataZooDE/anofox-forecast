#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"

#include "duckdb/function/scalar_function.hpp"

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

// Helper to write a double array into a LIST vector
static void WriteListFromArray(Vector &list_vec, idx_t row_idx, const double *data, size_t n) {
    auto offset = ListVector::GetListSize(list_vec);
    auto &child = ListVector::GetEntry(list_vec);
    ListVector::Reserve(list_vec, offset + n);
    auto child_data = FlatVector::GetData<double>(child);
    for (idx_t i = 0; i < n; i++) {
        child_data[offset + i] = data[i];
    }
    ListVector::SetListSize(list_vec, offset + n);
    auto list_data = ListVector::GetData(list_vec);
    list_data[row_idx].offset = offset;
    list_data[row_idx].length = n;
}

// ============================================================================
// ts_bootstrap_intervals
// ts_bootstrap_intervals(residuals[], forecasts[], n_paths, coverage, seed)
//   -> STRUCT(point[], lower[], upper[], coverage)
// ============================================================================

static void TsBootstrapIntervalsFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &residuals_vec = args.data[0];
    auto &forecasts_vec = args.data[1];
    auto &n_paths_vec = args.data[2];
    auto &coverage_vec = args.data[3];
    auto &seed_vec = args.data[4];
    idx_t count = args.size();

    UnifiedVectorFormat n_paths_data, coverage_data, seed_data;
    n_paths_vec.ToUnifiedFormat(count, n_paths_data);
    coverage_vec.ToUnifiedFormat(count, coverage_data);
    seed_vec.ToUnifiedFormat(count, seed_data);

    auto &struct_entries = StructVector::GetEntries(result);
    auto &point_out = *struct_entries[0];
    auto &lower_out = *struct_entries[1];
    auto &upper_out = *struct_entries[2];
    auto &coverage_out = *struct_entries[3];

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        auto np_idx = n_paths_data.sel->get_index(row_idx);
        auto cov_idx = coverage_data.sel->get_index(row_idx);
        auto seed_idx = seed_data.sel->get_index(row_idx);

        if (FlatVector::IsNull(residuals_vec, row_idx) ||
            FlatVector::IsNull(forecasts_vec, row_idx) ||
            !n_paths_data.validity.RowIsValid(np_idx) ||
            !coverage_data.validity.RowIsValid(cov_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> residuals, forecasts;
        ExtractListAsDouble(residuals_vec, row_idx, residuals);
        ExtractListAsDouble(forecasts_vec, row_idx, forecasts);
        int n_paths = UnifiedVectorFormat::GetData<int32_t>(n_paths_data)[np_idx];
        double coverage = UnifiedVectorFormat::GetData<double>(coverage_data)[cov_idx];
        int64_t seed = seed_data.validity.RowIsValid(seed_idx)
            ? UnifiedVectorFormat::GetData<int64_t>(seed_data)[seed_idx]
            : -1;

        BootstrapResultFFI bs_result = {};
        AnofoxError error;
        bool success = anofox_ts_bootstrap_intervals(
            residuals.data(), residuals.size(),
            forecasts.data(), forecasts.size(),
            n_paths, coverage, seed,
            &bs_result, &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        WriteListFromArray(point_out, row_idx, bs_result.point, bs_result.n_forecasts);
        WriteListFromArray(lower_out, row_idx, bs_result.lower, bs_result.n_forecasts);
        WriteListFromArray(upper_out, row_idx, bs_result.upper, bs_result.n_forecasts);
        FlatVector::GetData<double>(coverage_out)[row_idx] = bs_result.coverage;

        anofox_free_bootstrap_result(&bs_result);
    }
}

void RegisterTsBootstrapIntervalsFunction(ExtensionLoader &loader) {
    child_list_t<LogicalType> struct_children;
    struct_children.push_back(make_pair("point", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    struct_children.push_back(make_pair("lower", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    struct_children.push_back(make_pair("upper", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    struct_children.push_back(make_pair("coverage", LogicalType(LogicalTypeId::DOUBLE)));
    auto result_type = LogicalType::STRUCT(std::move(struct_children));

    // ts_bootstrap_intervals(residuals[], forecasts[], n_paths, coverage, seed)
    ScalarFunctionSet bs_set("ts_bootstrap_intervals");
    bs_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType(LogicalTypeId::INTEGER),
         LogicalType(LogicalTypeId::DOUBLE),
         LogicalType(LogicalTypeId::BIGINT)},
        result_type,
        TsBootstrapIntervalsFunction
    ));
    loader.RegisterFunction(bs_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_bootstrap_intervals");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType(LogicalTypeId::INTEGER),
         LogicalType(LogicalTypeId::DOUBLE),
         LogicalType(LogicalTypeId::BIGINT)},
        result_type,
        TsBootstrapIntervalsFunction
    ));
    loader.RegisterFunction(anofox_set);
}

// ============================================================================
// ts_bootstrap_quantiles
// ts_bootstrap_quantiles(residuals[], forecasts[], n_paths, quantiles[], seed)
//   -> STRUCT(point[], quantiles[], values[])
// ============================================================================

static void TsBootstrapQuantilesFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &residuals_vec = args.data[0];
    auto &forecasts_vec = args.data[1];
    auto &n_paths_vec = args.data[2];
    auto &quantiles_vec = args.data[3];
    auto &seed_vec = args.data[4];
    idx_t count = args.size();

    UnifiedVectorFormat n_paths_data, seed_data;
    n_paths_vec.ToUnifiedFormat(count, n_paths_data);
    seed_vec.ToUnifiedFormat(count, seed_data);

    auto &struct_entries = StructVector::GetEntries(result);
    auto &point_out = *struct_entries[0];
    auto &quantiles_out = *struct_entries[1];
    auto &values_out = *struct_entries[2];

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        auto np_idx = n_paths_data.sel->get_index(row_idx);
        auto seed_idx = seed_data.sel->get_index(row_idx);

        if (FlatVector::IsNull(residuals_vec, row_idx) ||
            FlatVector::IsNull(forecasts_vec, row_idx) ||
            FlatVector::IsNull(quantiles_vec, row_idx) ||
            !n_paths_data.validity.RowIsValid(np_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> residuals, forecasts, quantile_levels;
        ExtractListAsDouble(residuals_vec, row_idx, residuals);
        ExtractListAsDouble(forecasts_vec, row_idx, forecasts);
        ExtractListAsDouble(quantiles_vec, row_idx, quantile_levels);
        int n_paths = UnifiedVectorFormat::GetData<int32_t>(n_paths_data)[np_idx];
        int64_t seed = seed_data.validity.RowIsValid(seed_idx)
            ? UnifiedVectorFormat::GetData<int64_t>(seed_data)[seed_idx]
            : -1;

        BootstrapQuantileResultFFI bq_result = {};
        AnofoxError error;
        bool success = anofox_ts_bootstrap_quantiles(
            residuals.data(), residuals.size(),
            forecasts.data(), forecasts.size(),
            n_paths,
            quantile_levels.data(), quantile_levels.size(),
            seed,
            &bq_result, &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        WriteListFromArray(point_out, row_idx, bq_result.point, bq_result.n_forecasts);
        WriteListFromArray(quantiles_out, row_idx, bq_result.quantiles, bq_result.n_quantiles);

        // Values is a flat array of n_quantiles * n_forecasts — write as nested LIST of LISTs
        // We write it as a single flat list for simplicity (q0_t0, q0_t1, ..., q1_t0, q1_t1, ...)
        size_t total = bq_result.n_quantiles * bq_result.n_forecasts;
        WriteListFromArray(values_out, row_idx, bq_result.values, total);

        anofox_free_bootstrap_quantile_result(&bq_result);
    }
}

void RegisterTsBootstrapQuantilesFunction(ExtensionLoader &loader) {
    child_list_t<LogicalType> struct_children;
    struct_children.push_back(make_pair("point", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    struct_children.push_back(make_pair("quantiles", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    struct_children.push_back(make_pair("values", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    auto result_type = LogicalType::STRUCT(std::move(struct_children));

    // ts_bootstrap_quantiles(residuals[], forecasts[], n_paths, quantiles[], seed)
    ScalarFunctionSet bq_set("ts_bootstrap_quantiles");
    bq_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType(LogicalTypeId::INTEGER),
         LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType(LogicalTypeId::BIGINT)},
        result_type,
        TsBootstrapQuantilesFunction
    ));
    loader.RegisterFunction(bq_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_bootstrap_quantiles");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType(LogicalTypeId::INTEGER),
         LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType(LogicalTypeId::BIGINT)},
        result_type,
        TsBootstrapQuantilesFunction
    ));
    loader.RegisterFunction(anofox_set);
}

} // namespace duckdb
