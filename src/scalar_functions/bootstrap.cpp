#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"

#include "duckdb/function/scalar_function.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include "duckdb/common/types/vector.hpp"
#include "ts_list_marshal.hpp"

namespace duckdb {

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

    ListDoubleReader residuals_reader(residuals_vec, count);
    ListDoubleReader forecasts_reader(forecasts_vec, count);
    ScalarArgReader<int32_t> n_paths_reader(n_paths_vec, count);
    ScalarArgReader<double> coverage_reader(coverage_vec, count);
    ScalarArgReader<int64_t> seed_reader(seed_vec, count);

    auto &struct_entries = StructVector::GetEntries(result);
    auto &point_out = *struct_entries[0];
    auto &lower_out = *struct_entries[1];
    auto &upper_out = *struct_entries[2];
    auto &coverage_out = *struct_entries[3];

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (residuals_reader.IsNull(row_idx) || forecasts_reader.IsNull(row_idx) ||
            n_paths_reader.IsNull(row_idx) || coverage_reader.IsNull(row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> residuals, forecasts;
        vector<uint64_t> residuals_validity, forecasts_validity;
        residuals_reader.Read(row_idx, residuals, residuals_validity);
        forecasts_reader.Read(row_idx, forecasts, forecasts_validity);
        int n_paths = n_paths_reader.Get(row_idx);
        double coverage = coverage_reader.Get(row_idx);
        int64_t seed = seed_reader.IsNull(row_idx) ? -1 : seed_reader.Get(row_idx);

        BootstrapResultFFI bs_result = {};
        AnofoxError error;
        bool success = anofox_ts_bootstrap_intervals(
            residuals.data(), residuals_validity.empty() ? nullptr : residuals_validity.data(),
            residuals.size(),
            forecasts.data(), forecasts_validity.empty() ? nullptr : forecasts_validity.data(),
            forecasts.size(),
            n_paths, coverage, seed,
            &bs_result, &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        WriteDoubleListNanAsNull(point_out, row_idx, bs_result.point, bs_result.n_forecasts);
        WriteDoubleListNanAsNull(lower_out, row_idx, bs_result.lower, bs_result.n_forecasts);
        WriteDoubleListNanAsNull(upper_out, row_idx, bs_result.upper, bs_result.n_forecasts);
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
    {
        CreateScalarFunctionInfo info(bs_set);
        FunctionDescription desc;
        desc.description = "Generates bootstrap prediction intervals by resampling historical residuals. Returns STRUCT(point[], lower[], upper[], coverage).";
        desc.examples = {"ts_bootstrap_intervals(residuals, point_forecast, 1000, 0.90, 42)"};
        desc.categories = {"time-series", "uncertainty"};
        desc.parameter_names = {"residuals", "forecasts", "n_paths", "coverage", "seed"};
        desc.parameter_types = {
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType(LogicalTypeId::INTEGER),
            LogicalType(LogicalTypeId::DOUBLE),
            LogicalType(LogicalTypeId::BIGINT)
        };
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

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
    {
        CreateScalarFunctionInfo info(anofox_set);
        info.alias_of = "ts_bootstrap_intervals";
        FunctionDescription desc;
        desc.description = "Generates bootstrap prediction intervals by resampling historical residuals. Returns STRUCT(point[], lower[], upper[], coverage).";
        desc.examples = {"ts_bootstrap_intervals(residuals, point_forecast, 1000, 0.90, 42)"};
        desc.categories = {"time-series", "uncertainty"};
        desc.parameter_names = {"residuals", "forecasts", "n_paths", "coverage", "seed"};
        desc.parameter_types = {
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType(LogicalTypeId::INTEGER),
            LogicalType(LogicalTypeId::DOUBLE),
            LogicalType(LogicalTypeId::BIGINT)
        };
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
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

    ListDoubleReader residuals_reader(residuals_vec, count);
    ListDoubleReader forecasts_reader(forecasts_vec, count);
    ScalarArgReader<int32_t> n_paths_reader(n_paths_vec, count);
    ListDoubleReader quantiles_reader(quantiles_vec, count);
    ScalarArgReader<int64_t> seed_reader(seed_vec, count);

    auto &struct_entries = StructVector::GetEntries(result);
    auto &point_out = *struct_entries[0];
    auto &quantiles_out = *struct_entries[1];
    auto &values_out = *struct_entries[2];

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (residuals_reader.IsNull(row_idx) || forecasts_reader.IsNull(row_idx) ||
            quantiles_reader.IsNull(row_idx) || n_paths_reader.IsNull(row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> residuals, forecasts, quantile_levels;
        vector<uint64_t> residuals_validity, forecasts_validity;
        residuals_reader.Read(row_idx, residuals, residuals_validity);
        forecasts_reader.Read(row_idx, forecasts, forecasts_validity);
        quantiles_reader.ReadNoNulls(row_idx, quantile_levels, "ts_bootstrap_quantiles", "quantile_levels");
        int n_paths = n_paths_reader.Get(row_idx);
        int64_t seed = seed_reader.IsNull(row_idx) ? -1 : seed_reader.Get(row_idx);

        BootstrapQuantileResultFFI bq_result = {};
        AnofoxError error;
        bool success = anofox_ts_bootstrap_quantiles(
            residuals.data(), residuals_validity.empty() ? nullptr : residuals_validity.data(),
            residuals.size(),
            forecasts.data(), forecasts_validity.empty() ? nullptr : forecasts_validity.data(),
            forecasts.size(),
            n_paths,
            quantile_levels.data(), quantile_levels.size(),
            seed,
            &bq_result, &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        WriteDoubleListNanAsNull(point_out, row_idx, bq_result.point, bq_result.n_forecasts);
        WriteDoubleList(quantiles_out, row_idx, bq_result.quantiles, bq_result.n_quantiles);

        // Values is a flat array of n_quantiles * n_forecasts — write as nested LIST of LISTs
        // We write it as a single flat list for simplicity (q0_t0, q0_t1, ..., q1_t0, q1_t1, ...)
        size_t total = bq_result.n_quantiles * bq_result.n_forecasts;
        WriteDoubleListNanAsNull(values_out, row_idx, bq_result.values, total);

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
    {
        CreateScalarFunctionInfo info(bq_set);
        FunctionDescription desc;
        desc.description = "Generates bootstrap quantile intervals by resampling historical residuals. Returns STRUCT(point[], quantiles[], values[][]).";
        desc.examples = {"ts_bootstrap_quantiles(residuals, point_forecast, 1000, [0.1, 0.5, 0.9], 42)"};
        desc.categories = {"time-series", "uncertainty"};
        desc.parameter_names = {"residuals", "forecasts", "n_paths", "quantiles", "seed"};
        desc.parameter_types = {
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType(LogicalTypeId::INTEGER),
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType(LogicalTypeId::BIGINT)
        };
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

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
    {
        CreateScalarFunctionInfo info(anofox_set);
        info.alias_of = "ts_bootstrap_quantiles";
        FunctionDescription desc;
        desc.description = "Generates bootstrap quantile intervals by resampling historical residuals. Returns STRUCT(point[], quantiles[], values[][]).";
        desc.examples = {"ts_bootstrap_quantiles(residuals, point_forecast, 1000, [0.1, 0.5, 0.9], 42)"};
        desc.categories = {"time-series", "uncertainty"};
        desc.parameter_names = {"residuals", "forecasts", "n_paths", "quantiles", "seed"};
        desc.parameter_types = {
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType(LogicalTypeId::INTEGER),
            LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
            LogicalType(LogicalTypeId::BIGINT)
        };
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

} // namespace duckdb
