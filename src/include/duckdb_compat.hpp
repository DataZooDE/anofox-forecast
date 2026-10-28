#pragma once

// Helpers that compile against DuckDB 1.4 LTS, 1.5.x and 2.0.
//
// DuckDB's headers expose no version macro, so every helper detects the API it
// needs: from the shape of DuckDB's own callback typedefs (e.g. the arity of
// aggregate_initialize_t), or with SFINAE on the presence of a member (accessors
// such as GetReturnType() replace public fields across versions). Nothing here
// changes behaviour; on 1.4/1.5 the adapters forward to the callback unchanged.

#include "duckdb.hpp"
#include "duckdb/common/types/vector.hpp"
#include "duckdb/function/aggregate_function.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb/parser/parsed_expression.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/parser/parser.hpp"

// DuckDB 2.0 split the vector helpers (ListVector, StructVector, ...) out of
// vector.hpp into one header each.
#if defined(__has_include)
#if __has_include("duckdb/common/vector/list_vector.hpp")
#include "duckdb/common/vector/constant_vector.hpp"
#include "duckdb/common/vector/flat_vector.hpp"
#include "duckdb/common/vector/list_vector.hpp"
#include "duckdb/common/vector/string_vector.hpp"
#include "duckdb/common/vector/struct_vector.hpp"
#endif
// Up to 1.5 VariantValue (a different struct, without GetValue) lives in its own
// header; from 2.0 on it is declared in value.hpp.
#if __has_include("duckdb/common/types/variant_value.hpp")
#include "duckdb/common/types/variant_value.hpp"
#endif
#endif

#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

namespace duckdb {

//===--------------------------------------------------------------------===//
// Names
//===--------------------------------------------------------------------===//

//! The name type of STRUCT children and functions: std::string before DuckDB 2.0,
//! Identifier (explicitly constructible from a std::string) from 2.0 on.
using DuckName = typename child_list_t<LogicalType>::value_type::first_type;

//! A runtime string as a DuckDB name (struct child, function, function set).
inline DuckName AsDuckName(const std::string &name) {
	return DuckName(name);
}

//! Build a STRUCT child entry from a runtime string name.
inline std::pair<DuckName, LogicalType> MakeChild(const std::string &name, LogicalType type) {
	return std::make_pair(DuckName(name), std::move(type));
}

//! A DuckDB name (std::string or Identifier) as a std::string.
template <class NAME>
inline const std::string &NameString(const NAME &name) {
	return static_cast<const std::string &>(name);
}

//===--------------------------------------------------------------------===//
// Function accessors (public fields up to 1.5, accessors from 1.5 on)
//===--------------------------------------------------------------------===//

namespace compat_detail {
template <class T>
auto ReturnType(T &x, int) -> decltype(x.GetReturnType()) {
	return x.GetReturnType();
}
template <class T>
auto ReturnType(T &x, long) -> decltype((x.return_type)) {
	return x.return_type;
}
template <class T>
auto SetReturnType(T &x, LogicalType type, int) -> decltype(x.SetReturnType(type), void()) {
	x.SetReturnType(std::move(type));
}
template <class T>
void SetReturnType(T &x, LogicalType type, long) {
	x.return_type = std::move(type);
}
template <class T>
auto Arguments(T &x, int) -> decltype(x.GetArguments()) {
	return x.GetArguments();
}
template <class T>
auto Arguments(T &x, long) -> decltype((x.arguments)) {
	return x.arguments;
}
template <class T>
auto SetNullHandling(T &x, FunctionNullHandling value, int) -> decltype(x.SetNullHandling(value), void()) {
	x.SetNullHandling(value);
}
template <class T>
void SetNullHandling(T &x, FunctionNullHandling value, long) {
	x.null_handling = value;
}
template <class T>
auto SetOrderDependent(T &x, AggregateOrderDependent value, int) -> decltype(x.SetOrderDependent(value), void()) {
	x.SetOrderDependent(value);
}
template <class T>
void SetOrderDependent(T &x, AggregateOrderDependent value, long) {
	x.order_dependent = value;
}
template <class T>
auto SetArgumentType(T &x, idx_t i, LogicalType type, int) -> decltype(x.GetArguments()[i] = type, void()) {
	x.GetArguments()[i] = std::move(type);
}
template <class T>
auto SetArgumentType(T &x, idx_t i, LogicalType type, long)
    -> decltype(x.GetSignature().GetParameter(i).SetType(type), void()) {
	x.GetSignature().GetParameter(i).SetType(std::move(type));
}
template <class T>
auto SetArgumentType(T &x, idx_t i, LogicalType type, ...) -> decltype(x.arguments[i] = type, void()) {
	x.arguments[i] = std::move(type);
}
template <class T>
auto SetEntryName(T &x, const std::string &name, int) -> decltype(x.SetName(AsDuckName(name)), void()) {
	x.SetName(AsDuckName(name));
}
template <class T>
void SetEntryName(T &x, const std::string &name, long) {
	x.name = name;
}
template <class T>
auto SetSchema(T &x, const std::string &schema, int) -> decltype(x.SetSchema(AsDuckName(schema)), void()) {
	x.SetSchema(AsDuckName(schema));
}
template <class T>
void SetSchema(T &x, const std::string &schema, long) {
	x.schema = schema;
}
template <class T>
auto Name(T &x, int) -> decltype(x.GetName()) {
	return x.GetName();
}
template <class T>
auto Name(T &x, long) -> decltype((x.name)) {
	return x.name;
}
template <class T, class FN>
auto SetFunctionCallback(T &x, FN fn, int) -> decltype(x.SetFunctionCallback(fn), void()) {
	x.SetFunctionCallback(std::move(fn));
}
template <class T, class FN>
void SetFunctionCallback(T &x, FN fn, long) {
	x.function = std::move(fn);
}
template <class T>
auto BindInfo(T &x, int) -> decltype(x.BindInfo()) {
	return x.BindInfo();
}
template <class T>
auto BindInfo(T &x, long) -> decltype((x.bind_info)) {
	return x.bind_info;
}
} // namespace compat_detail

//! The return type of a function or an expression.
template <class T>
auto ReturnTypeOf(T &x) -> decltype(compat_detail::ReturnType(x, 0)) {
	return compat_detail::ReturnType(x, 0);
}
template <class T>
void SetReturnTypeOf(T &x, LogicalType type) {
	compat_detail::SetReturnType(x, std::move(type), 0);
}
//! The (mutable for a non-const function) argument types of a function.
template <class T>
auto ArgumentsOf(T &x) -> decltype(compat_detail::Arguments(x, 0)) {
	return compat_detail::Arguments(x, 0);
}
//! Change the type of argument i of a function.
template <class T>
void SetArgumentTypeOf(T &x, idx_t i, LogicalType type) {
	compat_detail::SetArgumentType(x, i, std::move(type), 0);
}
template <class T>
void SetNullHandlingOf(T &x, FunctionNullHandling value) {
	compat_detail::SetNullHandling(x, value, 0);
}
template <class T>
void SetOrderDependentOf(T &x, AggregateOrderDependent value) {
	compat_detail::SetOrderDependent(x, value, 0);
}
//! The name of a function or catalog entry as a std::string.
template <class T>
std::string NameOf(T &x) {
	return NameString(compat_detail::Name(x, 0));
}
//! Replace the execute callback of a scalar function.
template <class T, class FN>
void SetFunctionCallbackOf(T &x, FN fn) {
	compat_detail::SetFunctionCallback(x, std::move(fn), 0);
}
namespace compat_detail {
template <class P>
auto MakeParser(P *, int) -> decltype(P::GetBuiltinParser()) {
	return P::GetBuiltinParser();
}
template <class P>
P MakeParser(P *, long) {
	return P();
}
} // namespace compat_detail

//! A Parser with the default options (DuckDB 2.0 has no default constructor).
inline Parser MakeParser() {
	return compat_detail::MakeParser(static_cast<Parser *>(nullptr), 0);
}

//! Set the schema and name of a CreateInfo.
template <class T>
void SetCreateInfoName(T &info, const std::string &schema, const std::string &name) {
	compat_detail::SetSchema(info, schema, 0);
	compat_detail::SetEntryName(info, name, 0);
}
//! The bind data of a BoundFunctionExpression.
template <class T>
auto BindInfoOf(T &x) -> decltype(compat_detail::BindInfo(x, 0)) {
	return compat_detail::BindInfo(x, 0);
}

//===--------------------------------------------------------------------===//
// Vector writes (DuckDB 2.0 made FlatVector::GetData / Validity read-only)
//===--------------------------------------------------------------------===//

namespace compat_detail {
template <class T, class FV>
auto FlatData(FV *, Vector &v, int) -> decltype(FV::template GetDataMutable<T>(v)) {
	return FV::template GetDataMutable<T>(v);
}
template <class T, class FV>
T *FlatData(FV *, Vector &v, long) {
	return FV::template GetData<T>(v);
}
template <class FV>
auto FlatValidity(FV *, Vector &v, int) -> decltype(FV::ValidityMutable(v)) {
	return FV::ValidityMutable(v);
}
template <class FV>
ValidityMask &FlatValidity(FV *, Vector &v, long) {
	return FV::Validity(v);
}
} // namespace compat_detail

//! Writable data of a flat vector.
template <class T>
inline T *FlatDataMut(Vector &v) {
	return compat_detail::FlatData<T>(static_cast<FlatVector *>(nullptr), v, 0);
}

//! Writable validity mask of a flat vector.
inline ValidityMask &FlatValidityMut(Vector &v) {
	return compat_detail::FlatValidity(static_cast<FlatVector *>(nullptr), v, 0);
}

//! A STRUCT child vector: StructVector::GetEntries holds unique_ptr<Vector> before
//! DuckDB 2.0 and Vector from 2.0 on.
inline Vector &StructEntry(Vector &entry) {
	return entry;
}
inline const Vector &StructEntry(const Vector &entry) {
	return entry;
}
inline Vector &StructEntry(const unique_ptr<Vector> &entry) {
	return *entry;
}
//! The children of a STRUCT vector, as returned by StructVector::GetEntries.
using StructEntries = typename std::remove_reference<decltype(StructVector::GetEntries(std::declval<Vector &>()))>::type;

//===--------------------------------------------------------------------===//
// Aggregate and scalar callbacks
//===--------------------------------------------------------------------===//

//! Our aggregate Initialize callbacks placement-new one state.
typedef void (*anofox_aggregate_init_t)(data_ptr_t state);
//! Our aggregate Finalize callbacks (DuckDB 2.0 passes an AggregateFinalizeInputData,
//! which derives from AggregateInputData).
typedef void (*anofox_aggregate_finalize_t)(Vector &state, AggregateInputData &aggr_input_data, Vector &result,
                                            idx_t count, idx_t offset);

namespace compat_detail {
template <class FN>
struct FunctionTraits;
template <class R, class... ARGS>
struct FunctionTraits<R (*)(ARGS...)> {
	template <size_t I>
	using Arg = typename std::tuple_element<I, std::tuple<ARGS...>>::type;
};

template <class FN>
struct InitAdapter;
// DuckDB < 2.0: void(const AggregateFunction &, data_ptr_t)
template <class FUNCTION, class STATE>
struct InitAdapter<void (*)(FUNCTION, STATE)> {
	template <anofox_aggregate_init_t INIT>
	static void Call(FUNCTION, STATE state) {
		INIT(state);
	}
};
// DuckDB 2.0: void(AggregateStateInput &, data_ptr_t *states, idx_t count)
template <class INPUT, class STATES, class COUNT>
struct InitAdapter<void (*)(INPUT, STATES, COUNT)> {
	template <anofox_aggregate_init_t INIT>
	static void Call(INPUT, STATES states, COUNT count) {
		for (idx_t i = 0; i < idx_t(count); i++) {
			INIT(states[i]);
		}
	}
};

template <class FN>
struct BindAdapter;
// DuckDB < 2.0: unique_ptr<FunctionData>(ClientContext &, FUNCTION &, vector<unique_ptr<Expression>> &)
template <class R, class FUNCTION>
struct BindAdapter<R (*)(ClientContext &, FUNCTION &, vector<unique_ptr<Expression>> &)> {
	using Function = FUNCTION;
	static constexpr bool kNewApi = false;
	template <R (*BIND)(ClientContext &, Function &, vector<unique_ptr<Expression>> &)>
	static R Call(ClientContext &context, Function &function, vector<unique_ptr<Expression>> &arguments) {
		return BIND(context, function, arguments);
	}
};
// DuckDB 2.0: unique_ptr<FunctionData>(Bind{Aggregate,Scalar}FunctionInput &)
template <class R, class INPUT>
struct BindAdapter<R (*)(INPUT &)> {
	using Function = typename std::remove_reference<decltype(std::declval<INPUT &>().GetBoundFunction())>::type;
	static constexpr bool kNewApi = true;
	template <R (*BIND)(ClientContext &, Function &, vector<unique_ptr<Expression>> &)>
	static R Call(INPUT &input) {
		return BIND(input.GetClientContext(), input.GetBoundFunction(), input.GetArguments());
	}
};
} // namespace compat_detail

//! The input of aggregate Finalize callbacks.
using AggregateFinalizeInput = typename std::remove_reference<
    typename compat_detail::FunctionTraits<aggregate_finalize_t>::template Arg<1>>::type;

//! Adapt one of our Finalize callbacks to aggregate_finalize_t.
template <anofox_aggregate_finalize_t FINALIZE>
void FinalizeAdapter(Vector &state, AggregateFinalizeInput &aggr_input_data, Vector &result, idx_t count,
                     idx_t offset) {
	FINALIZE(state, aggr_input_data, result, count, offset);
}

//! The function a bind callback may modify (e.g. to set the return type):
//! AggregateFunction / ScalarFunction before DuckDB 2.0, the Bound* variant from 2.0 on.
using AggregateBindFunction = compat_detail::BindAdapter<bind_aggregate_function_t>::Function;
using ScalarBindFunction = compat_detail::BindAdapter<bind_scalar_function_t>::Function;

namespace compat_detail {
template <class T>
auto SetFallible(T &x, int) -> decltype(x.SetFallible(), void()) {
	x.SetFallible();
}
template <class T>
void SetFallible(T &, long) {
}
template <class T, class = void>
struct HasSetFallible : std::false_type {};
template <class T>
struct HasSetFallible<T, decltype(std::declval<T &>().SetFallible(), void())> : std::true_type {};
} // namespace compat_detail

// On DuckDB 2.0 the no-op fallback above must never be chosen: an unmarked function
// that throws turns every user-facing error into an InternalException.
static_assert(!compat_detail::BindAdapter<bind_scalar_function_t>::kNewApi ||
                  compat_detail::HasSetFallible<ScalarFunction>::value,
              "DuckDB 2.0 API without ScalarFunction::SetFallible(): update MarkFallible");

//! Mark a scalar function that can throw at execution time. DuckDB 2.0 rejects an
//! execution error from a function that is not marked (InternalException); on
//! earlier versions this is a no-op, so their behaviour is unchanged.
inline void MarkFallible(ScalarFunction &function) {
	if (compat_detail::BindAdapter<bind_scalar_function_t>::kNewApi) {
		compat_detail::SetFallible(function, 0);
	}
}

//! MarkFallible for a function passed by value (e.g. straight into AddFunction).
inline ScalarFunction Fallible(ScalarFunction function) {
	MarkFallible(function);
	return function;
}

//===--------------------------------------------------------------------===//
// anofox_forecast extras (same rule: detect by shape, forward to the old API on 1.x)
//===--------------------------------------------------------------------===//

namespace compat_detail {
// DataChunk::SetCardinality only sets the chunk count; 2.0 vectors carry their own size and
// SetChildCardinality resizes them too (1.5.x: documented forwarder; 1.4: absent).
template <class C>
auto SetOutputCardinality(C &chunk, idx_t count, int) -> decltype(chunk.SetChildCardinality(count), void()) {
	chunk.SetChildCardinality(count);
}
template <class C>
void SetOutputCardinality(C &chunk, idx_t count, long) {
	chunk.SetCardinality(count);
}
// Vector::ToUnifiedFormat / Flatten lost their count argument in 2.0 (the count overloads are deprecated).
template <class V>
auto ToUnified(V &v, idx_t, UnifiedVectorFormat &data, int) -> decltype(v.ToUnifiedFormat(data), void()) {
	v.ToUnifiedFormat(data);
}
template <class V>
void ToUnified(V &v, idx_t count, UnifiedVectorFormat &data, long) {
	v.ToUnifiedFormat(count, data);
}
template <class V>
auto Flatten(V &v, idx_t, int) -> decltype(v.Flatten(), void()) {
	v.Flatten();
}
template <class V>
void Flatten(V &v, idx_t count, long) {
	v.Flatten(count);
}
// ListVector::GetEntry became GetChild (read) / GetChildMutable (write) in 2.0.
template <class LV>
auto ListChild(LV *, const Vector &v, int) -> decltype(LV::GetChild(v)) {
	return LV::GetChild(v);
}
template <class LV>
const Vector &ListChild(LV *, const Vector &v, long) {
	return LV::GetEntry(v);
}
template <class LV>
auto ListChildMut(LV *, Vector &v, int) -> decltype(LV::GetChildMutable(v)) {
	return LV::GetChildMutable(v);
}
template <class LV>
Vector &ListChildMut(LV *, Vector &v, long) {
	return LV::GetEntry(v);
}
// The stability field became an accessor (SetStability) in 1.5 / 2.0.
template <class T>
auto SetStability(T &x, FunctionStability value, int) -> decltype(x.SetStability(value), void()) {
	x.SetStability(value);
}
template <class T>
void SetStability(T &x, FunctionStability value, long) {
	x.stability = value;
}
// TableFunction::named_parameters became keyword-only parameters on the signature in 2.0.
template <class F, class = void>
struct HasKeywordOnly : std::false_type {};
template <class F>
struct HasKeywordOnly<F, decltype(std::declval<F &>().GetSignature().AddKeywordOnly(
                                      std::declval<DuckName>(), std::declval<LogicalType>(), std::declval<Value>()),
                                  void())> : std::true_type {};
template <class F>
void AddNamedParameter(F &fn, const std::string &name, const LogicalType &type, std::true_type) {
	// A typed-NULL default keeps the parameter optional (2.0 keyword-only parameters
	// without a default are required).
	fn.GetSignature().AddKeywordOnly(AsDuckName(name), type, Value(type));
}
template <class F>
void AddNamedParameter(F &fn, const std::string &name, const LogicalType &type, std::false_type) {
	fn.named_parameters[name] = type;
}
// Parser::ParseExpressionList is static before 2.0 and an instance method from 2.0 on.
template <class P>
auto ParseExpressionList(P *, const std::string &list, int) -> decltype(P::ParseExpressionList(list)) {
	return P::ParseExpressionList(list);
}
template <class P>
vector<unique_ptr<ParsedExpression>> ParseExpressionList(P *, const std::string &list, long) {
	auto parser = ::duckdb::MakeParser();
	return parser.ParseExpressionList(list);
}
// ClientContext::Query(query, bool allow_stream_result) lost its bool overload in 2.0. There
// QueryParameters has no bool constructor, and the result-format overload with no format runs
// the query to completion with the default (blocking, retained) parameters.
template <class C>
auto QueryMaterialized(C &context, const std::string &query, int) -> decltype(context.Query(query, false)) {
	return context.Query(query, false);
}
template <class C>
auto QueryMaterialized(C &context, const std::string &query, long) -> decltype(context.Query(query, nullptr)) {
	return context.Query(query, nullptr);
}
} // namespace compat_detail

//! Set the cardinality of an output chunk (and, on 2.0, the size of its vectors).
inline void SetOutputCardinality(DataChunk &chunk, idx_t count) {
	compat_detail::SetOutputCardinality(chunk, count, 0);
}

//! Unified format of an input vector (count is ignored on 2.0, where vectors know their size).
inline void ToUnified(Vector &v, idx_t count, UnifiedVectorFormat &data) {
	compat_detail::ToUnified(v, count, data, 0);
}

//! Flatten a vector (count is ignored on 2.0).
inline void FlattenVector(Vector &v, idx_t count) {
	compat_detail::Flatten(v, count, 0);
}

//! The child vector of a LIST vector, read-only.
inline const Vector &ListChild(const Vector &v) {
	return compat_detail::ListChild(static_cast<ListVector *>(nullptr), v, 0);
}
//! The child vector of a LIST vector, writable.
inline Vector &ListChildMut(Vector &v) {
	return compat_detail::ListChildMut(static_cast<ListVector *>(nullptr), v, 0);
}

template <class T>
void SetStabilityOf(T &x, FunctionStability value) {
	compat_detail::SetStability(x, value, 0);
}

//! The element type of the `names` out-parameter of a table function bind callback:
//! std::string before DuckDB 2.0, Identifier from 2.0 on.
using TableBindNames = typename std::remove_reference<
    typename compat_detail::FunctionTraits<table_function_bind_t>::template Arg<3>>::type;
using TableBindName = typename TableBindNames::value_type;

//! A runtime string as a table-bind column name.
inline TableBindName MakeBindName(const std::string &name) {
	return TableBindName(name);
}

//! True when named table-function parameters are keyword-only signature parameters
//! (DuckDB 2.0). They then carry a typed-NULL default, so bind sees a NULL value for
//! every parameter the caller left out and must skip it.
static constexpr bool kNamedParamsHaveNullDefault = compat_detail::HasKeywordOnly<TableFunction>::value;

//! Declare an optional named parameter of a table function.
inline void AddNamedParameter(TableFunction &fn, const std::string &name, const LogicalType &type) {
	compat_detail::AddNamedParameter(fn, name, type, compat_detail::HasKeywordOnly<TableFunction>());
}

//! Run a query to completion (no streaming result).
inline unique_ptr<QueryResult> QueryMaterialized(ClientContext &context, const std::string &query) {
	return compat_detail::QueryMaterialized(context, query, 0);
}

//! Parse a comma-separated expression list.
inline vector<unique_ptr<ParsedExpression>> ParseExpressionList(const std::string &list) {
	return compat_detail::ParseExpressionList(static_cast<Parser *>(nullptr), list, 0);
}


//===--------------------------------------------------------------------===//
// VARIANT options
//===--------------------------------------------------------------------===//

namespace compat_detail {
template <class VV>
auto UnwrapVariant(const Value &v, int) -> decltype(VV::GetValue(v)) {
	return VV::GetValue(v);
}
template <class VV>
Value UnwrapVariant(const Value &, long) {
	throw InvalidInputException("params of type VARIANT need DuckDB 2.0 or later; pass a STRUCT "
	                            "({'seasonal_period': 7, 'model': 'AAA'}) or a MAP instead");
}
template <class VV, class = void>
struct HasVariantGetValue : std::false_type {};
template <class VV>
struct HasVariantGetValue<VV, decltype(void(VV::GetValue(std::declval<const Value &>())))> : std::true_type {};
} // namespace compat_detail

//! Whether VARIANT values can be unwrapped (VariantValue::GetValue, DuckDB 2.0+).
constexpr bool kHasVariantUnwrap = compat_detail::HasVariantGetValue<VariantValue>::value;

//! A params/options argument as the STRUCT or MAP the parsers understand. A VARIANT
//! (e.g. `{'seasonal_period': 7}::VARIANT`, or a value read from a VARIANT column) is
//! unwrapped to the value it holds, and a MAP(VARCHAR, VARIANT) gets its values
//! unwrapped. Without this the parsers, which branch on MAP / STRUCT, silently
//! ignored every key of a VARIANT. On DuckDB < 2.0 a VARIANT raises instead.
inline Value NormalizeParamsValue(const Value &params) {
	if (params.IsNull()) {
		return params;
	}
	if (params.type().id() == LogicalTypeId::VARIANT) {
		return NormalizeParamsValue(compat_detail::UnwrapVariant<VariantValue>(params, 0));
	}
	if (params.type().id() == LogicalTypeId::MAP &&
	    MapType::ValueType(params.type()).id() == LogicalTypeId::VARIANT) {
		auto &entries = MapValue::GetChildren(params);
		vector<Value> keys;
		vector<Value> values;
		for (auto &entry : entries) {
			auto &kv = StructValue::GetChildren(entry);
			keys.push_back(kv[0]);
			if (kv[1].IsNull()) {
				values.push_back(Value(LogicalType::VARCHAR));
			} else {
				// Before 2.0 a VARIANT entry can't be unwrapped, but its text form is
				// what the parsers always read from MAP values.
				values.push_back(Value(kHasVariantUnwrap ? NormalizeParamsValue(kv[1]).ToString() : kv[1].ToString()));
			}
		}
		return Value::MAP(MapType::KeyType(params.type()), LogicalType::VARCHAR, std::move(keys), std::move(values));
	}
	return params;
}

//! A params/options argument as MAP(VARCHAR, VARCHAR), the form the SQL macros and the
//! MAP-typed natives read: STRUCT children, MAP values and VARIANT contents become their
//! text form (lists as '[7, 30]'), NULL becomes an empty map.
inline Value ParamsAsVarcharMap(const Value &params_in) {
	const Value params = NormalizeParamsValue(params_in);
	vector<Value> keys;
	vector<Value> values;
	auto add = [&](const string &key, const Value &val) {
		keys.push_back(Value(key));
		values.push_back(val.IsNull() ? Value(LogicalType::VARCHAR) : Value(val.ToString()));
	};
	if (params.IsNull()) {
		// no params
	} else if (params.type().id() == LogicalTypeId::MAP) {
		for (auto &entry : MapValue::GetChildren(params)) {
			auto &kv = StructValue::GetChildren(entry);
			add(kv[0].ToString(), kv[1]);
		}
	} else if (params.type().id() == LogicalTypeId::STRUCT) {
		auto &children = StructValue::GetChildren(params);
		auto &child_types = StructType::GetChildTypes(params.type());
		for (idx_t i = 0; i < child_types.size(); i++) {
			add(NameString(child_types[i].first), children[i]);
		}
	} else {
		throw InvalidInputException("params must be a STRUCT or MAP (e.g. {'alpha': 0.1}), got %s",
		                            params.type().ToString());
	}
	return Value::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR, std::move(keys), std::move(values));
}

} // namespace duckdb

//! Wrap our callbacks for an AggregateFunction / ScalarFunction registration.
#define ANOFOX_AGG_INIT(...) (&::duckdb::compat_detail::InitAdapter<::duckdb::aggregate_initialize_t>::Call<__VA_ARGS__>)
#define ANOFOX_AGG_FINALIZE(...) (&::duckdb::FinalizeAdapter<__VA_ARGS__>)
#define ANOFOX_AGG_BIND(...)                                                                                      \
	(&::duckdb::compat_detail::BindAdapter<::duckdb::bind_aggregate_function_t>::Call<__VA_ARGS__>)
#define ANOFOX_SCALAR_BIND(...)                                                                                   \
	(&::duckdb::compat_detail::BindAdapter<::duckdb::bind_scalar_function_t>::Call<__VA_ARGS__>)
