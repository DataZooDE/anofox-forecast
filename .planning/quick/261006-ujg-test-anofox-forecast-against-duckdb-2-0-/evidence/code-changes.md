## Code changes required for DuckDB 2.0

All changes below are behavior-preserving and gated via
`__has_include("duckdb/common/identifier.hpp")` (a header that only exists on 2.0 — never a
guessed/invented macro), so the same source compiles on both v1.5.5 and v2.0-cyanoptera. The
**compiles on v1.5.5** column is the result of the Task 3 dual-compile check: checking out
every touched `src/` file into the v1.5.5 baseline tree and rebuilding it there (confirmed
`EXIT=0`, 0 errors, and the tracer-style `ts_forecast_by` AutoETS query still returns the
correct 280 rows against that rebuilt baseline binary).

| # | Change | Guarded? | Compiles on v1.5.5? |
|---|--------|----------|----------------------|
| 1 | CMakeLists.txt wasm-target detection (pre-existing bug) | n/a | yes (same fix applied to BASE by the orchestrator) |
| 2 | ListVector/StructVector/FlatVector/StringVector header split | yes | yes |
| 3 | FlatVector::GetData is read-only; writes need GetDataMutable | yes | yes |
| 4 | FlatVector::Validity is read-only; writes need ValidityMutable | yes | yes |
| 5 | StructVector::GetEntries returns vector<Vector>&, not vector<unique_ptr<Vector>>& | yes (added after Task 3 dual-compile caught the break — see §5) | yes, after fix in commit `7f7a821` |
| 6 | ScalarFunction/TableFunction/AggregateFunction .stability / .null_handling / .return_type members | not needed (setters exist on both) | yes |
| 7 | table_function_bind_t "names" vector<string> -> vector<Identifier> | yes | yes |
| 8 | Scalar/aggregate bind callback signature -> BindScalarFunctionInput&/BindAggregateFunctionInput& | yes | yes |
| 9 | aggregate_finalize_t second param -> AggregateFinalizeInputData& | yes | yes |
| 10 | BoundFunctionExpression::bind_info is private (BindInfo() accessor) | yes | yes |
| 11 | child_list_t<T> key type Identifier (dynamic struct/pair field names) | n/a (`.c_str()` trick works on both without a macro) | yes |
| 12 | QueryParameters dropped the implicit bool constructor | yes | yes |
| 13 | TableFunction::named_parameters -> FunctionSignature::AddKeywordOnly | yes | yes |
| 14 | Parser lost its default constructor; ParseExpressionList became an instance method | yes | yes |
| 15 | CreateInfo's public schema/name fields -> SetSchema/SetName(Identifier) | yes | yes |
| 16 | _ts_forecast_scalar needs SetFallible() to surface validation errors (not an API *removal*, a stricter 2.0 runtime check) | not needed (SetFallible exists on both) | yes |

### 1. CMakeLists.txt wasm-target detection (pre-existing bug, not DuckDB-2.0-specific)
See commit `eef531e` — unrelated to the 2.0 API surface, should go upstream separately.

### 2. ListVector/StructVector/FlatVector/StringVector header split
**API change:** DuckDB 2.0 moved `ListVector`, `StructVector`, `FlatVector`, `StringVector`
(and friends) out of `duckdb/common/types/vector.hpp` into dedicated headers under
`duckdb/common/vector/{list,struct,flat,string}_vector.hpp`. v1.5.x still declares them
inline in `vector.hpp`.
**Fix:** Added `#if __has_include("duckdb/common/vector/flat_vector.hpp")`-guarded explicit
includes of the dedicated headers to `src/include/anofox_forecast_extension.hpp` (included
by every table/aggregate/scalar function file as the first include), so every translation
unit gets them regardless of what else it includes.
**Guarded:** yes (`__has_include`).

### 3. FlatVector::GetData(Vector&) is now read-only; writes need GetDataMutable
**API change:** `FlatVector::GetData<T>(Vector &vector)` always returns `const T *` in 2.0.
A new `FlatVector::GetDataMutable<T>(Vector &vector)` returns the writable `T *`. v1.5.x has
no `GetDataMutable`; its `GetData(Vector&)` overload already returned a mutable pointer for
a non-const `Vector&` argument.
**Fix:** Macro `ANOFOX_FLATVECTOR_WRITE` in `anofox_forecast_extension.hpp` resolves to
`FlatVector::GetDataMutable` on 2.0, `FlatVector::GetData` on 1.5.x. Applied at every call
site where the returned pointer is written through — both the same-line pattern
(`FlatVector::GetData<T>(expr)[idx] = value;`, 177 sites) and the declare-then-write-later
pattern (`auto data = FlatVector::GetData<T>(vec); ... data[i] = x;` /
`memcpy(data + off, ...)`, 136 more sites found via a second detection pass) — across
ts_classify_seasonality_agg.cpp, ts_detect_periods_agg.cpp, ts_forecast_agg.cpp,
ts_changepoints_agg.cpp, ts_data_quality_agg.cpp, ts_features_agg.cpp, ts_stats_agg.cpp,
bootstrap.cpp, conformal.cpp, metrics.cpp, diagnostics.cpp, ts_changepoints.cpp,
ts_detrend.cpp, ts_peaks.cpp, ts_features.cpp, ts_seasonality.cpp, ts_stats.cpp,
ts_periods.cpp, ts_decomposition.cpp, ts_imputation.cpp, ts_filter.cpp, ts_forecast.cpp.
Read-only call sites were left untouched since a `const T*` read compiles unchanged on both
versions.
**Guarded:** yes.

### 4. FlatVector::Validity(Vector&) is now read-only; writes need ValidityMutable
**API change:** Same split as #3, for `FlatVector::Validity`. v1.5.x's `Validity(Vector&)`
overload already returned a mutable `ValidityMask&`.
**Fix:** Macro `ANOFOX_FLATVECTOR_VALIDITY_WRITE`, applied to the 2 call sites that actually
call `.SetInvalid(...)` through the result (`ts_imputation.cpp`); the other 13
`FlatVector::Validity(...)` call sites across the codebase are read-only (`.RowIsValid(...)`)
and needed no change.
**Guarded:** yes.

### 5. StructVector::GetEntries returns vector<Vector>&, not vector<unique_ptr<Vector>>&
**API change:** `StructVector::GetEntries(Vector&)` returns `vector<Vector>&` in 2.0 (direct
value, not smart pointer); v1.5.x returns `vector<unique_ptr<Vector>>&`.
**Fix (two passes):**
- First pass (commit `fa44c2b`) mechanically removed the dereference `*` at every call site
  (`*children[idx]` -> `children[idx]`) to get the 2.0 native build green — 313 sites across
  20 files.
- The Task 3 dual-compile check caught that this breaks v1.5.5 exactly as anticipated
  ("`unique_ptr<Vector>` cannot be converted to `const Vector&`", 75 distinct errors). Fixed
  in commit `7f7a821` by introducing `ANOFOX_STRUCT_ENTRY(entries, idx)` (expands to
  `entries[idx]` on 2.0, `*entries[idx]` on v1.5.x) and mechanically re-wrapping all 328
  `StructVector::GetEntries()`-result indexing sites with it, across the same 20 files plus
  3 more occurrences the second detection pass caught (`children_init` in
  `ts_periods.cpp`, `out_entries`/`profile_entries` in `conformal.cpp`).
**Guarded:** yes, after the fix. Re-verified the dual-compile check: `EXIT=0`, 0 errors,
tracer-style forecast still returns 280 rows against the v1.5.5-rebuilt binary.

### 6. ScalarFunction/TableFunction/AggregateFunction .stability / .null_handling / .return_type members
**API change:** these data members are still *public* on both versions (confirmed by
inspecting both headers) — not a real 2.0-only break. `SetStability`/`SetNullHandling`/
`SetReturnType` setters exist on both; converting the direct-member-write sites to them was
done proactively for future-proofing. 44 `.stability =` sites (ts_stats.cpp, ts_periods.cpp,
ts_forecast.cpp), 6 `.null_handling =` sites (ts_forecast_ensemble_native.cpp,
ts_forecast_inspect_scalar.cpp, ts_ensemble_inspect_native.cpp x2, ts_forecast_scalar.cpp),
3 `.return_type =` sites (ts_forecast_ensemble_native.cpp, ts_ensemble_inspect_native.cpp x2,
ts_forecast_agg.cpp, ts_features_agg.cpp x2) -> `.SetReturnType(...)`.
**Guarded:** not needed.

### 7. table_function_bind_t "names" vector<string> -> vector<Identifier>
**API change:** `table_function_bind_t`'s `names` out-parameter and
`TableFunctionBindInput::input_table_names` changed from `vector<string>` to the new
case-insensitive `vector<Identifier>` wrapper type (explicit string constructor/operator) in
2.0. v1.5.x has no `Identifier` type.
**Fix:**
- Macro `ANOFOX_BIND_NAMES_VEC` (`vector<Identifier>` on 2.0, `vector<string>` on 1.5.x)
  replaces the `vector<string> &names` bind-function parameter in 21 files / 26 bind function
  signatures (exact function-pointer-type match is required for the implicit conversion to
  `table_function_bind_t` to compile).
- Overloaded helper `ToColumnName(const string&)` / `ToColumnName(const char*)` /
  `ToColumnName(const Identifier&)` (the Identifier overload only compiles in on 2.0) converts
  an element down to a plain `string`, used at every extraction site that assigns an
  `input_table_names[i]` element (or a ternary mixing it with a string-literal fallback) into
  a plain `string` member/variable: ts_aggregate_hierarchy.cpp (3), ts_features_native.cpp
  (1), ts_split_keys.cpp (2), ts_combine_keys.cpp (2), ts_cv_hydrate_native.cpp (3),
  ts_mstl_decomposition_native.cpp (1), ts_stats.cpp (1), ts_changepoints.cpp (3),
  ts_forecast_native.cpp (1), ts_forecast_panel_native.cpp (1), ts_cv_forecast_native.cpp (4),
  ts_metrics_native.cpp (2), ts_cv_folds_native.cpp (6), ts_cv_hydrate_native.cpp (2 more).
- The reverse helper `ToBindName(const string&)` (-> `Identifier` on 2.0, identity on 1.5.x)
  is used wherever a plain `string` is pushed into a `names`-typed vector or assigned into an
  `Identifier`-typed field: `CreateMacroInfo::name`/`alias_of` in `ts_macros.cpp`,
  `ScalarFunctionSet`'s name constructor in `diagnostics.cpp`, plus ~20 `names.push_back(...)`
  call sites across the table_functions files above.
- `names.push_back(input.input_table_names[i])` and
  `input.input_table_names[i] == col_name` call sites needed **no** change: the element types
  already match (push_back) or `Identifier` provides `operator==` against `string`/`const
  char*` (comparison) on 2.0.
- One false-positive avoided: `GetFeatureNames()`/`GetScalarFeatureNames()` (in
  ts_features_native.cpp, ts_features_agg.cpp, ts_features.cpp) also have a local variable
  named `names`, but it is an unrelated `vector<string>` (Rust FFI feature-name list, not a
  bind-names parameter) — confirmed by context before wrapping, and NOT touched.
**Guarded:** yes.

### 8. Scalar/aggregate bind callback signature change
**API change:** `bind_scalar_function_t`/`bind_aggregate_function_t` changed from
`(ClientContext&, ScalarFunction&/AggregateFunction&, vector<unique_ptr<Expression>>&)` to a
single `BindScalarFunctionInput&`/`BindAggregateFunctionInput&`, with `context`/
`bound_function`/`arguments` pulled out via `GetClientContext()`/`GetBoundFunction()`/
`GetArguments()`. v1.5.x has neither wrapper type.
**Fix:** Macro pairs `ANOFOX_SCALAR_BIND_SIG`/`_PREAMBLE` and `ANOFOX_AGG_BIND_SIG`/
`_PREAMBLE` declare the right parameter list per version and locally bind the same 3 names
the existing bodies already used (a no-op preamble on 1.5.x, since its parameter list already
has those names). Applied to 4 scalar bind functions (`TsForecastScalarBind`,
`TsForecastEnsembleNativeBind`, `TsEnsembleInspectNativeBind`,
`TsAutoEnsembleInspectNativeBind`) and 3 aggregate bind functions (`TsForecastAggBind`,
`TsFeaturesAggBind3`, `TsFeaturesAggBind4`). `Expression::return_type` also became protected
on 2.0 (read via `GetReturnType()`); helper `ExprReturnType(const Expression&)` bridges both.
**Guarded:** yes.

### 9. aggregate_finalize_t second parameter -> AggregateFinalizeInputData&
**API change:** `aggregate_finalize_t` takes `AggregateFinalizeInputData&` (a subclass of
`AggregateInputData`) in 2.0, not a plain `AggregateInputData&`. v1.5.x has no
`AggregateFinalizeInputData` type.
**Fix:** Macro `ANOFOX_AGG_FINALIZE_INPUT` (the exact-matching type per version) applied to
the second parameter of the 7 `TsXxxAggFinalize` top-level functions (one per
aggregate_functions/*.cpp file). The inner templated `Finalize(STATE&, T&,
AggregateFinalizeData&)` helpers are unrelated (different, unchanged type) and untouched.
**Guarded:** yes.

### 10. BoundFunctionExpression::bind_info is private
**API change:** `bind_info` became a private field with `BindInfo()`/`BindInfoMutable()`
accessors in 2.0. v1.5.x has it as a public field, no accessors.
**Fix:** Macro `ANOFOX_BIND_INFO(expr)` (`.BindInfo()` on 2.0, `.bind_info` on 1.5.x) at all
4 call sites (ts_ensemble_inspect_native.cpp x2, ts_forecast_ensemble_native.cpp,
ts_forecast_scalar.cpp).
**Guarded:** yes.

### 11. child_list_t<T> key type is Identifier
**API change:** `child_list_t<T> = vector<pair<Identifier, T>>` in 2.0 (was `vector<pair<string,
T>>`). Breaks `make_pair(dynamic_string_variable, type)` calls used to build STRUCT return
types with runtime-determined field names (`pair<string, LogicalType>` has no implicit
converting constructor to `pair<Identifier, LogicalType>` since `Identifier`'s `string`
constructor is explicit). String-**literal** first args are unaffected: `make_pair("literal",
type)` deduces `pair<const char*, LogicalType>`, which *does* implicitly convert (Identifier's
`const char*` constructor is non-explicit) — this covers the overwhelming majority (70 of 74)
of `child_list_t` push sites in this codebase, which use literal field names.
**Fix:** `.c_str()` on the dynamic-string argument, which makes `make_pair` deduce
`pair<const char*, LogicalType>` on both versions — no macro needed. 4 sites across
ts_features_agg.cpp (3) and ts_features.cpp (1), the only places building per-feature-name
STRUCT columns from a runtime string.
**Guarded:** not needed (works unconditionally on both).

### 12. QueryParameters dropped the implicit bool constructor
**API change:** v1.5.x's `QueryParameters(bool allow_streaming)` implicit constructor is gone
in 2.0; `QueryParameters` now has a single `result_eagerness` field
(`ResultEagerness::FORCED`/`AUTO`) instead of `output_type`/`memory_type`.
**Fix:** Helper `AnofoxQueryParams(bool allow_streaming)` maps the same boolean onto whichever
shape exists. 1 call site (`ts_fill_forward_operator.cpp`, `context.Query(query,
AnofoxQueryParams(false))`).
**Guarded:** yes.

### 13. TableFunction::named_parameters -> FunctionSignature::AddKeywordOnly
**API change:** the flat `named_parameters` map (`name -> LogicalType`) used to *declare* a
table function's accepted named parameters was removed in 2.0 in favor of Python-style
KEYWORD_ONLY parameters on `FunctionSignature` (`AddKeywordOnly`/`AddParameter` with a
`FunctionParameterKind`). v1.5.x only has the flat map. (Note: `TableFunctionBindInput::
named_parameters`, read at *bind time* to look up what the caller actually passed, is
unchanged on both versions and needed no fix — only the declaration side moved.)
**Fix:** Macro `ANOFOX_ADD_NAMED_PARAM(func, name, type)` (`func.GetSignature().
AddKeywordOnly(name, type)` on 2.0, `func.named_parameters[name] = type` on 1.5.x). 3 sites
(`ts_split_keys.cpp` x2, `ts_validate_separator.cpp` x1).
**Guarded:** yes.

### 14. Parser lost its default constructor; ParseExpressionList became an instance method
**API change:** v1.5.x's `Parser()` is default-constructible and `Parser::ParseExpressionList`
is `static`. 2.0's `Parser` requires a `ClientContext&` or explicit `ParserOptions`
(`Parser::GetBuiltinParser()` is the context-free factory for extension-internal use), and
`ParseExpressionList` became an instance method.
**Fix:** Helpers `AnofoxMakeParser()` and `AnofoxParseExpressionList(Parser&, const string&)`
bridge both shapes. 2 call sites in `ts_macros.cpp`'s table-macro registration helper
(`CreateTableMacro`), which also needed explicit `#include "duckdb/parser/parser.hpp"` +
`"duckdb/parser/parsed_expression.hpp"` added to `anofox_forecast_extension.hpp` (these
weren't previously pulled in by every translation unit including that header).
**Guarded:** yes.

### 15. CreateInfo's public schema/name fields -> SetSchema/SetName(Identifier)
**API change:** `CreateInfo::schema`/`name` were public `string` fields on v1.5.x; 2.0
replaced them with a `QualifiedName` plus `SetSchema(Identifier)`/`SetName(Identifier)`
setters (no public fields).
**Fix:** Macros `ANOFOX_SET_INFO_SCHEMA`/`ANOFOX_SET_INFO_NAME` route to the setters on 2.0,
the field assignment on 1.5.x. 4 sites in `ts_macros.cpp`'s `CreateTableMacro`/
`RegisterTsTableMacros` (schema default, macro name, and the `anofox_fcst_`-prefixed alias's
name + `alias_of`).
**Guarded:** yes.

### 16. _ts_forecast_scalar needs SetFallible() on DuckDB 2.0
**Behavior change (not an API removal):** `SetFallible()` exists on both versions with the
same documented meaning ("can throw runtime errors"), but 2.0 *enforces* it strictly: a
scalar function whose Execute callback throws a user-facing validation error (e.g.
`InvalidInputException`) without having called `SetFallible()` at registration gets its error
wrapped as an opaque `INTERNAL Error: ... the function is not marked as fallible` instead of
surfacing the real message. v1.5.5 surfaces the original error either way.
**Fix:** `func.SetFallible();` added to `_ts_forecast_scalar`'s registration in
`ts_forecast_scalar.cpp` (commit `f90fa32`). Fixed `test/sql/ts_forecast_laplace.test`
(previously failing); `test/sql/ts_forecast_ets_model.test` still has one unrelated,
pre-existing failure at a different assertion (see `test-2.0-only.md`).
**Not fixed speculatively:** grep confirms zero other `SetFallible()` calls anywhere in this
extension, and several other scalar/aggregate functions also throw `InvalidInputException`
from their Execute/Finalize path (e.g. in `metrics.cpp`, `conformal.cpp`, `diagnostics.cpp`).
None of those code paths are exercised by a currently-failing test on this branch, so adding
`SetFallible()` to them was deliberately left as a follow-up rather than changed speculatively
here (per the fix-what's-needed scope boundary) — **tracked as a required upstream-facing
follow-up**, not done.
**Guarded:** not needed (`SetFallible` exists unconditionally on both).

## Also observed, not required to fix (informational only)

- **Single-arrow lambda deprecation is now a hard error.** DuckDB 2.0 treats the old
  `x -> expr` lambda syntax as a Binder *error* by default (v1.5.5 only warns and still
  executes). Confirmed directly: `SELECT list_transform([1,2,3], x -> x + 1)` succeeds with a
  warning on v1.5.5, hard-errors on v2.0 ("Deprecated lambda arrow (->) detected... before
  DuckDB's next release", `SET lambda_syntax='ENABLE_SINGLE_ARROW'` reverts it). This is an
  intended DuckDB 2.0 change, not an extension bug; see `test-2.0-only.md` category (b)
  (`test/sql/ts_changepoints.test`).
- **Implicit string-to-identifier conversion in table function args is now deprecated.**
  Passing an unquoted identifier where a string literal is expected in a table function call
  (e.g. `ts_forecast_by(m4s, ...)` instead of `ts_forecast_by('m4s', ...)`) now prints
  `Deprecated implicit conversion of unbound identifiers to strings in table function
  arguments detected... Use SET table_function_identifier_conversion='ENABLE_IMPLICIT_STRING'
  to revert`. Still works (warning, not an error) on 2.0; this driver's SQL micro-benchmark
  uses the unquoted form and keeps working, but the warning text was the reason the
  `sqlbench` checksum differs between v1.5.5 and v2.0 for every label (see `sqlbench-compare.md`
  and `261006-ujg-BENCHMARK.md` — confirmed by direct re-run that the actual returned data is
  byte-identical; only the printed CLI banner/warning/table-rendering text differs).
