#pragma once

#include "duckdb.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/parsed_expression.hpp"

// DuckDB 2.0 split FlatVector::GetData(Vector&) so it always returns a
// read-only pointer; writes must go through the new FlatVector::GetDataMutable.
// v1.5.x's FlatVector::GetData(Vector&) overload already returned a mutable
// pointer and has no GetDataMutable. ANOFOX_FLATVECTOR_WRITE resolves to
// whichever one is writable on the version actually being compiled, so call
// sites that WRITE through the returned pointer keep compiling unchanged on
// both; read-only call sites are untouched and keep using FlatVector::GetData.
#if __has_include("duckdb/common/vector/flat_vector.hpp")
#include "duckdb/common/vector/flat_vector.hpp"
#include "duckdb/common/vector/list_vector.hpp"
#include "duckdb/common/vector/struct_vector.hpp"
#include "duckdb/common/vector/string_vector.hpp"
#define ANOFOX_FLATVECTOR_WRITE ::duckdb::FlatVector::GetDataMutable
#define ANOFOX_FLATVECTOR_VALIDITY_WRITE ::duckdb::FlatVector::ValidityMutable
#else
#define ANOFOX_FLATVECTOR_WRITE ::duckdb::FlatVector::GetData
#define ANOFOX_FLATVECTOR_VALIDITY_WRITE ::duckdb::FlatVector::Validity
#endif

// DuckDB 2.0 changed table_function_bind_t's "names" out-parameter (and
// TableFunctionBindInput::input_table_names) from vector<string> to
// vector<Identifier>, a case-insensitive column-name wrapper. v1.5.x has no
// Identifier type at all. ANOFOX_BIND_NAMES_VEC is the exact-matching
// parameter type for our bind function signatures on each version; ToColumnName
// converts either element type down to a plain std::string at call sites that
// need one (e.g. assigning into a plain string member).
#if __has_include("duckdb/common/identifier.hpp")
#include "duckdb/common/identifier.hpp"
#define ANOFOX_BIND_NAMES_VEC ::duckdb::vector<::duckdb::Identifier>
#else
#define ANOFOX_BIND_NAMES_VEC ::duckdb::vector<::duckdb::string>
#endif

namespace duckdb {

static inline string ToColumnName(const string &value) {
	return value;
}
static inline string ToColumnName(const char *value) {
	return string(value);
}
#if __has_include("duckdb/common/identifier.hpp")
static inline string ToColumnName(const Identifier &value) {
	return value.GetIdentifierName();
}
// The reverse direction: build a names-vector element from a plain string.
// Identifier's string constructor is explicit (by design, per its own doc
// comment), so `names.push_back(some_std_string)` does not compile on 2.0
// without this. On 1.5.x this is the identity function.
static inline Identifier ToBindName(const string &value) {
	return Identifier(value);
}
#else
static inline const string &ToBindName(const string &value) {
	return value;
}
#endif

// DuckDB 2.0's QueryParameters dropped the implicit bool (allow_streaming)
// constructor v1.5.x had, replacing output_type/memory_type with a single
// result_eagerness field. AnofoxQueryParams maps the same "allow streaming?"
// bool onto whichever shape QueryParameters has on the version being built.
#if __has_include("duckdb/common/identifier.hpp")
static inline QueryParameters AnofoxQueryParams(bool allow_streaming) {
	QueryParameters params;
	params.result_eagerness = allow_streaming ? ResultEagerness::AUTO : ResultEagerness::FORCED;
	return params;
}
#else
static inline QueryParameters AnofoxQueryParams(bool allow_streaming) {
	return QueryParameters(allow_streaming);
}
#endif

// DuckDB 2.0 made Expression::return_type protected; read access now goes
// through GetReturnType(). v1.5.x never had the accessor, only the public
// field.
#if __has_include("duckdb/common/identifier.hpp")
static inline const LogicalType &ExprReturnType(const Expression &expr) {
	return expr.GetReturnType();
}
#else
static inline const LogicalType &ExprReturnType(const Expression &expr) {
	return expr.return_type;
}
#endif

} // namespace duckdb

// DuckDB 2.0 changed scalar-function bind callbacks from
// (ClientContext&, ScalarFunction&, vector<unique_ptr<Expression>>&) to a
// single BindScalarFunctionInput&, with context/bound_function/arguments
// pulled out of it via accessors. v1.5.x has no BindScalarFunctionInput at
// all. These macros let a single function body compile on both: the
// signature macro declares the right parameter list per version, and the
// preamble macro (a no-op on 1.5.x) locally binds the same three names
// (context, bound_function, arguments) the existing function bodies use.
#if __has_include("duckdb/common/identifier.hpp")
#define ANOFOX_SCALAR_BIND_SIG(fn_name) \
	static ::duckdb::unique_ptr<::duckdb::FunctionData> fn_name(::duckdb::BindScalarFunctionInput &anofox_bind_input)
#define ANOFOX_SCALAR_BIND_PREAMBLE \
	auto &context = anofox_bind_input.GetClientContext(); \
	auto &bound_function = anofox_bind_input.GetBoundFunction(); \
	auto &arguments = anofox_bind_input.GetArguments();
#else
#define ANOFOX_SCALAR_BIND_SIG(fn_name)                                                                              \
	static ::duckdb::unique_ptr<::duckdb::FunctionData> fn_name(                                                     \
	    ::duckdb::ClientContext &context, ::duckdb::ScalarFunction &bound_function,                                  \
	    ::duckdb::vector<::duckdb::unique_ptr<::duckdb::Expression>> &arguments)
#define ANOFOX_SCALAR_BIND_PREAMBLE
#endif

// DuckDB 2.0 made BoundFunctionExpression::bind_info private; read access now
// goes through BindInfo(). v1.5.x never had the accessor, only the public
// field.
#if __has_include("duckdb/common/identifier.hpp")
#define ANOFOX_BIND_INFO(expr) ((expr).BindInfo())
#else
#define ANOFOX_BIND_INFO(expr) ((expr).bind_info)
#endif

// DuckDB 2.0's StructVector::GetEntries returns vector<Vector>& (indexing gives a
// Vector& directly); v1.5.x returns vector<unique_ptr<Vector>>& (indexing gives a
// unique_ptr<Vector>&, which must be dereferenced to get a Vector&).
// ANOFOX_STRUCT_ENTRY(entries, idx) is the version-correct way to get a Vector&
// out of a StructVector::GetEntries() result on either version.
#if __has_include("duckdb/common/identifier.hpp")
#define ANOFOX_STRUCT_ENTRY(entries, idx) ((entries)[(idx)])
#else
#define ANOFOX_STRUCT_ENTRY(entries, idx) (*(entries)[(idx)])
#endif

// DuckDB 2.0 replaced SimpleNamedParameterFunction::named_parameters (a flat
// name->LogicalType map) with Python-style KEYWORD_ONLY parameters declared
// on FunctionSignature via AddKeywordOnly. v1.5.x has no FunctionSignature /
// GetSignature() at all. ANOFOX_ADD_NAMED_PARAM(func, name, type) declares a
// named parameter the same way on both.
#if __has_include("duckdb/common/identifier.hpp")
#define ANOFOX_ADD_NAMED_PARAM(func, name, type) ((func).GetSignature().AddKeywordOnly((name), (type)))
#else
#define ANOFOX_ADD_NAMED_PARAM(func, name, type) ((func).named_parameters[(name)] = (type))
#endif

// DuckDB 2.0's aggregate_finalize_t takes AggregateFinalizeInputData& (a
// subclass of AggregateInputData) instead of a plain AggregateInputData&.
// v1.5.x has no AggregateFinalizeInputData type. ANOFOX_AGG_FINALIZE_INPUT is
// the exact-matching second-parameter type for a TsXxxAggFinalize function on
// each version (the type is a strict superset on 2.0, so bodies that only
// touch the AggregateInputData-level members work unchanged on both).
#if __has_include("duckdb/common/identifier.hpp")
#define ANOFOX_AGG_FINALIZE_INPUT ::duckdb::AggregateFinalizeInputData
#else
#define ANOFOX_AGG_FINALIZE_INPUT ::duckdb::AggregateInputData
#endif

// DuckDB 2.0 changed aggregate-function bind callbacks from
// (ClientContext&, AggregateFunction&, vector<unique_ptr<Expression>>&) to a
// single BindAggregateFunctionInput&. v1.5.x has no BindAggregateFunctionInput.
// Mirrors ANOFOX_SCALAR_BIND_SIG/_PREAMBLE for aggregates.
#if __has_include("duckdb/common/identifier.hpp")
#define ANOFOX_AGG_BIND_SIG(fn_name)                                                                                 \
	static ::duckdb::unique_ptr<::duckdb::FunctionData> fn_name(::duckdb::BindAggregateFunctionInput &anofox_bind_input)
#define ANOFOX_AGG_BIND_PREAMBLE \
	auto &context = anofox_bind_input.GetClientContext(); \
	auto &function = anofox_bind_input.GetBoundFunction(); \
	auto &arguments = anofox_bind_input.GetArguments();
#else
#define ANOFOX_AGG_BIND_SIG(fn_name)                                                                                 \
	static ::duckdb::unique_ptr<::duckdb::FunctionData> fn_name(                                                     \
	    ::duckdb::ClientContext &context, ::duckdb::AggregateFunction &function,                                    \
	    ::duckdb::vector<::duckdb::unique_ptr<::duckdb::Expression>> &arguments)
#define ANOFOX_AGG_BIND_PREAMBLE
#endif

// DuckDB 2.0 replaced CreateInfo's public `schema`/`name` string fields with
// SetSchema(Identifier)/SetName(Identifier) plus a QualifiedName. v1.5.x only
// has the public fields. Values passed through these macros may be a string
// literal (implicitly convertible to Identifier on 2.0, to string on 1.5.x -
// no wrap needed) or the result of ToBindName (already the right element
// type for either version).
#if __has_include("duckdb/common/identifier.hpp")
#define ANOFOX_SET_INFO_NAME(info, value) ((info).SetName(value))
#define ANOFOX_SET_INFO_SCHEMA(info, value) ((info).SetSchema(value))
#else
#define ANOFOX_SET_INFO_NAME(info, value) ((info).name = (value))
#define ANOFOX_SET_INFO_SCHEMA(info, value) ((info).schema = (value))
#endif

// DuckDB 2.0 requires an explicit Parser (via GetBuiltinParser() for
// extension-internal, context-free parsing) and made ParseExpressionList an
// instance method. v1.5.x default-constructs Parser and keeps
// ParseExpressionList static. AnofoxMakeParser/AnofoxParseExpressionList
// bridge both shapes.
namespace duckdb {
#if __has_include("duckdb/common/identifier.hpp")
static inline Parser AnofoxMakeParser() {
	return Parser::GetBuiltinParser();
}
static inline vector<unique_ptr<ParsedExpression>> AnofoxParseExpressionList(Parser &parser, const string &s) {
	return parser.ParseExpressionList(s);
}
#else
static inline Parser AnofoxMakeParser() {
	return Parser();
}
static inline vector<unique_ptr<ParsedExpression>> AnofoxParseExpressionList(Parser &parser, const string &s) {
	(void)parser;
	return Parser::ParseExpressionList(s);
}
#endif
} // namespace duckdb

namespace duckdb {

// Forward declarations for function registration
void RegisterTsStatsFunction(ExtensionLoader &loader);
void RegisterTsStatsByFunction(ExtensionLoader &loader);
void RegisterTsQualityReportFunction(ExtensionLoader &loader);
void RegisterTsStatsSummaryFunction(ExtensionLoader &loader);
void RegisterTsDataQualityFunction(ExtensionLoader &loader);
void RegisterTsDataQualitySummaryFunction(ExtensionLoader &loader);
void RegisterTsFillGapsFunction(ExtensionLoader &loader);
void RegisterTsFillGapsOperatorFunction(ExtensionLoader &loader);
void RegisterTsFillGapsNativeFunction(ExtensionLoader &loader);
void RegisterTsFillForwardFunction(ExtensionLoader &loader);
void RegisterTsFillForwardOperatorFunction(ExtensionLoader &loader);
void RegisterTsFillForwardNativeFunction(ExtensionLoader &loader);
void RegisterTsDropConstantFunction(ExtensionLoader &loader);
void RegisterTsDropShortFunction(ExtensionLoader &loader);
void RegisterTsDropLeadingZerosFunction(ExtensionLoader &loader);
void RegisterTsDropTrailingZerosFunction(ExtensionLoader &loader);
void RegisterTsDropEdgeZerosFunction(ExtensionLoader &loader);
void RegisterTsFillNullsConstFunction(ExtensionLoader &loader);
void RegisterTsFillNullsForwardFunction(ExtensionLoader &loader);
void RegisterTsFillNullsBackwardFunction(ExtensionLoader &loader);
void RegisterTsFillNullsMeanFunction(ExtensionLoader &loader);
void RegisterTsDiffFunction(ExtensionLoader &loader);
void RegisterTsDetectSeasonalityFunction(ExtensionLoader &loader);
void RegisterTsAnalyzeSeasonalityFunction(ExtensionLoader &loader);
void RegisterTsMstlDecompositionFunction(ExtensionLoader &loader);

// Period detection functions (fdars-core integration)
void RegisterTsDetectPeriodsFunction(ExtensionLoader &loader);
void RegisterTsEstimatePeriodFftFunction(ExtensionLoader &loader);
void RegisterTsEstimatePeriodAcfFunction(ExtensionLoader &loader);
void RegisterTsDetectMultiplePeriodsFunction(ExtensionLoader &loader);
void RegisterTsDetectPeriodsAggFunction(ExtensionLoader &loader);
void RegisterTsAutoperiodFunction(ExtensionLoader &loader);
void RegisterTsCfdAutoperiodFunction(ExtensionLoader &loader);
void RegisterTsLombScargleFunction(ExtensionLoader &loader);
void RegisterTsAicPeriodFunction(ExtensionLoader &loader);
void RegisterTsSsaPeriodFunction(ExtensionLoader &loader);
void RegisterTsStlPeriodFunction(ExtensionLoader &loader);
void RegisterTsMatrixProfilePeriodFunction(ExtensionLoader &loader);
void RegisterTsSazedPeriodFunction(ExtensionLoader &loader);

// Peak detection functions (fdars-core integration)
void RegisterTsDetectPeaksFunction(ExtensionLoader &loader);
void RegisterTsAnalyzePeakTimingFunction(ExtensionLoader &loader);

// Detrending and decomposition functions (fdars-core integration)
void RegisterTsDetrendFunction(ExtensionLoader &loader);
void RegisterTsDecomposeSeasonalFunction(ExtensionLoader &loader);

// Extended seasonality functions (fdars-core integration)
void RegisterTsSeasonalStrengthFunction(ExtensionLoader &loader);
void RegisterTsSeasonalStrengthWindowedFunction(ExtensionLoader &loader);
void RegisterTsClassifySeasonalityFunction(ExtensionLoader &loader);
void RegisterTsClassifySeasonalityAggFunction(ExtensionLoader &loader);
void RegisterTsDetectSeasonalityChangesFunction(ExtensionLoader &loader);
void RegisterTsInstantaneousPeriodFunction(ExtensionLoader &loader);
void RegisterTsDetectAmplitudeModulationFunction(ExtensionLoader &loader);
void RegisterTsDetectChangepointsFunction(ExtensionLoader &loader);
void RegisterTsDetectChangepointsBocpdFunction(ExtensionLoader &loader);
void RegisterTsDetectChangepointsByFunction(ExtensionLoader &loader);
void RegisterTsDetectChangepointsAggFunction(ExtensionLoader &loader);
void RegisterTsFeaturesFunction(ExtensionLoader &loader);
void RegisterTsFeaturesListFunction(ExtensionLoader &loader);
void RegisterTsFeaturesAggFunction(ExtensionLoader &loader);
void RegisterTsStatsAggFunction(ExtensionLoader &loader);
void RegisterTsDataQualityAggFunction(ExtensionLoader &loader);
void RegisterTsFeaturesConfigFromJsonFunction(ExtensionLoader &loader);
void RegisterTsFeaturesConfigFromCsvFunction(ExtensionLoader &loader);
void RegisterTsFeaturesConfigTemplateFunction(ExtensionLoader &loader);
void RegisterTsForecastFunction(ExtensionLoader &loader);
void RegisterTsForecastByFunction(ExtensionLoader &loader);
void RegisterTsForecastAggFunction(ExtensionLoader &loader);
void RegisterTsForecastScalarFunction(ExtensionLoader &loader);
void RegisterTsForecastInspectScalarFunction(ExtensionLoader &loader);
void RegisterTsMaeFunction(ExtensionLoader &loader);
void RegisterTsMseFunction(ExtensionLoader &loader);
void RegisterTsRmseFunction(ExtensionLoader &loader);
void RegisterTsMapeFunction(ExtensionLoader &loader);
void RegisterTsSmapeFunction(ExtensionLoader &loader);
void RegisterTsMaseFunction(ExtensionLoader &loader);
void RegisterTsR2Function(ExtensionLoader &loader);
void RegisterTsBiasFunction(ExtensionLoader &loader);
void RegisterTsRmaeFunction(ExtensionLoader &loader);
void RegisterTsQuantileLossFunction(ExtensionLoader &loader);
void RegisterTsMqlossFunction(ExtensionLoader &loader);
void RegisterTsCoverageFunction(ExtensionLoader &loader);
void RegisterTsEstimateBacktestMemoryFunction(ExtensionLoader &loader);

// Conformal prediction functions
void RegisterTsConformalQuantileFunction(ExtensionLoader &loader);
void RegisterTsConformalIntervalsFunction(ExtensionLoader &loader);
void RegisterTsConformalPredictFunction(ExtensionLoader &loader);
void RegisterTsConformalPredictAsymmetricFunction(ExtensionLoader &loader);
void RegisterTsMeanIntervalWidthFunction(ExtensionLoader &loader);

// Conformal API v2 (Learn/Apply pattern)
void RegisterTsConformalLearnFunction(ExtensionLoader &loader);
void RegisterTsConformalApplyFunction(ExtensionLoader &loader);
void RegisterTsConformalCoverageFunction(ExtensionLoader &loader);
void RegisterTsConformalEvaluateFunction(ExtensionLoader &loader);

// Per-step conformal prediction
void RegisterTsConformalPredictPerStepFunction(ExtensionLoader &loader);

// Bootstrap prediction
void RegisterTsBootstrapIntervalsFunction(ExtensionLoader &loader);
void RegisterTsBootstrapQuantilesFunction(ExtensionLoader &loader);

// Statistical diagnostic tests (Phase 1: STAT-01..03, RESID-01..04)
void RegisterTsAdfFunction(ExtensionLoader &loader);
void RegisterTsKpssFunction(ExtensionLoader &loader);
void RegisterTsStationarityFunction(ExtensionLoader &loader);
void RegisterTsLjungBoxFunction(ExtensionLoader &loader);
void RegisterTsDurbinWatsonFunction(ExtensionLoader &loader);
void RegisterTsJarqueBeraFunction(ExtensionLoader &loader);
void RegisterTsResidualDiagnosticsFunction(ExtensionLoader &loader);

// Table macros
void RegisterTsTableMacros(ExtensionLoader &loader);

// Native table functions (streaming)
void RegisterTsBacktestNativeFunction(ExtensionLoader &loader);
void RegisterTsForecastNativeFunction(ExtensionLoader &loader);
void RegisterTsCvSplitNativeFunction(ExtensionLoader &loader);
void RegisterTsCvForecastNativeFunction(ExtensionLoader &loader);
void RegisterTsCvFoldsNativeFunction(ExtensionLoader &loader);
void RegisterTsCvHydrateNativeFunction(ExtensionLoader &loader);
void RegisterTsMstlDecompositionNativeFunction(ExtensionLoader &loader);
void RegisterTsFeaturesNativeFunction(ExtensionLoader &loader);
void RegisterTsDetectChangepointsNativeFunction(ExtensionLoader &loader);
void RegisterTsAggregateHierarchyFunction(ExtensionLoader &loader);
void RegisterTsCombineKeysFunction(ExtensionLoader &loader);
void RegisterTsSplitKeysFunction(ExtensionLoader &loader);
void RegisterTsValidateSeparatorFunction(ExtensionLoader &loader);
void RegisterTsMetricsNativeFunction(ExtensionLoader &loader);
void RegisterTsMaseNativeFunction(ExtensionLoader &loader);
void RegisterTsRmaeNativeFunction(ExtensionLoader &loader);
void RegisterTsCoverageNativeFunction(ExtensionLoader &loader);
void RegisterTsQuantileLossNativeFunction(ExtensionLoader &loader);

// Extension class
class AnofoxForecastExtension : public Extension {
public:
    void Load(ExtensionLoader &loader) override;
    std::string Name() override;
    std::string Version() const override;
};

} // namespace duckdb
