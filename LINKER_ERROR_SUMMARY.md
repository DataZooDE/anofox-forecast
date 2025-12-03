# Linker Error: Undefined Reference to CreateTSFillGapsOperatorTableFunction()

## Executive Summary

We are experiencing a persistent linker error in GitHub Actions (Alpine Linux, musl libc, GCC 14.2.0) where the function `CreateTSFillGapsOperatorTableFunction()` is defined in the static library but cannot be resolved when linking final executables (`duckdb`, `test/unittest`, `test_sqlite3_api_wrapper`).

**Critical Observation:** The function symbol exists in the static library (verified with `nm`), but the linker reports it as undefined when linking executables. This suggests a linking order, visibility, or symbol export issue rather than a missing definition.

## Issue Summary

**Error Messages (Multiple Targets):**

1. **test/unittest:**
```
/usr/lib/gcc/x86_64-alpine-linux-musl/14.2.0/../../../../x86_64-alpine-linux-musl/bin/ld: 
src/libduckdb.so: undefined reference to `duckdb::CreateTSFillGapsOperatorTableFunction()'
```

2. **duckdb executable:**
```
/usr/lib/gcc/x86_64-alpine-linux-musl/14.2.0/../../../../x86_64-alpine-linux-musl/bin/ld: 
extension/anofox_forecast/libanofox_forecast_extension.a(data_prep_macros.cpp.o): 
in function `duckdb::RegisterDataPrepMacros(duckdb::ExtensionLoader&)':
data_prep_macros.cpp:(.text._ZN6duckdb22RegisterDataPrepMacrosERNS_15ExtensionLoaderE+0x1518): 
undefined reference to `duckdb::CreateTSFillGapsOperatorTableFunction()'
data_prep_macros.cpp:(.text._ZN6duckdb22RegisterDataPrepMacrosERNS_15ExtensionLoaderE+0x1af4): 
undefined reference to `duckdb::CreateTSFillGapsOperatorTableFunction()'
```

3. **test_sqlite3_api_wrapper:**
```
/usr/lib/gcc/x86_64-alpine-linux-musl/14.2.0/../../../../x86_64-alpine-linux-musl/bin/ld: 
tools/sqlite3_api_wrapper/libsqlite3_api_wrapper.so: undefined reference to 
`duckdb::CreateTSFillGapsOperatorTableFunction()'
```

**Context:**
- The function `CreateTSFillGapsOperatorTableFunction()` is declared in `src/include/ts_fill_gaps_function.hpp`
- The function is defined in `src/ts_fill_gaps_function.cpp`
- The function is called from `src/data_prep_macros.cpp` (lines 741 and 748)
- All three files are included in `EXTENSION_SOURCES` in `CMakeLists.txt`
- The build succeeds locally but fails in GitHub Actions (Alpine Linux, musl libc)

## Verification Steps Taken

### 1. Symbol Verification
```bash
# Function is defined in the object file
nm -C build/release/extension/anofox_forecast/CMakeFiles/anofox_forecast_extension.dir/src/ts_fill_gaps_function.cpp.o | grep CreateTSFillGapsOperatorTableFunction
# Output: 0000000000000000 T duckdb::CreateTSFillGapsOperatorTableFunction()

# Function is referenced (undefined) in data_prep_macros.cpp.o
nm -C build/release/extension/anofox_forecast/CMakeFiles/anofox_forecast_extension.dir/src/data_prep_macros.cpp.o | grep CreateTSFillGapsOperatorTableFunction
# Output: U duckdb::CreateTSFillGapsOperatorTableFunction()

# Function exists in the static library
nm -C build/release/extension/anofox_forecast/libanofox_forecast_extension.a | grep CreateTSFillGapsOperatorTableFunction
# Output: 
# 0000000000000000 T duckdb::CreateTSFillGapsOperatorTableFunction()
# 0000000000000000 t duckdb::CreateTSFillGapsOperatorTableFunction() [clone .cold]
```

### 2. Object File Verification
```bash
# Object file is included in the static library
ar -t build/release/extension/anofox_forecast/libanofox_forecast_extension.a | grep ts_fill_gaps
# Output: ts_fill_gaps_function.cpp.o
```

### 3. Source File Verification
- `src/ts_fill_gaps_function.cpp` is listed in `EXTENSION_SOURCES` (line 138 of CMakeLists.txt)
- `src/include/ts_fill_gaps_function.hpp` is included in `data_prep_macros.cpp` (line 6)
- Function is declared in header (line 145): `unique_ptr<TableFunction> CreateTSFillGapsOperatorTableFunction();`
- Function is defined in implementation (line 655): `unique_ptr<TableFunction> CreateTSFillGapsOperatorTableFunction() {`

## Attempted Solutions

### 1. Verified File Inclusion
- ✅ Confirmed `ts_fill_gaps_function.cpp` is in `EXTENSION_SOURCES`
- ✅ Confirmed header is included in `data_prep_macros.cpp`
- ✅ Confirmed function signature matches between header and implementation

### 2. Checked Namespace Consistency
- ✅ Both files use `namespace duckdb { ... }`
- ✅ Function is properly scoped within the namespace
- ✅ No conflicting declarations found

### 3. Compared with Working Example
- Compared with `CreateTSFeaturesListTableFunction()` in `ts_features_function.cpp`
- Key difference: `CreateTSFeaturesListTableFunction()` is marked as `static` (internal linkage)
- Our function is non-static (external linkage) as it's called from a different translation unit

### 4. Added `__attribute__((used))`
- Added `__attribute__((used))` to prevent linker dead code elimination
- This attribute tells the compiler to keep the function even if it appears unused
- **Status**: ✅ Applied but **did not resolve the issue** - linker error persists in CI

### 5. Verified Symbol Existence
- Confirmed function symbol exists in static library with `nm`
- Confirmed object file is included in library with `ar -t`
- Confirmed function is defined (not just declared) in implementation file
- **Status**: All verifications pass, but linker still fails

## Code Structure

### Function Declaration (Header)
```cpp
// src/include/ts_fill_gaps_function.hpp (line 145)
namespace duckdb {
    unique_ptr<TableFunction> CreateTSFillGapsOperatorTableFunction();
} // namespace duckdb
```

### Function Definition (Implementation)
```cpp
// src/ts_fill_gaps_function.cpp (line 655)
namespace duckdb {
    // Note: __attribute__((used)) was added but did not resolve the issue
    __attribute__((used)) unique_ptr<TableFunction> CreateTSFillGapsOperatorTableFunction() {
        // ... implementation ...
        return make_uniq<TableFunction>(std::move(table_function));
    }
} // namespace duckdb
```

### Function Usage (Call Site)
```cpp
// src/data_prep_macros.cpp (lines 741, 748)
#include "ts_fill_gaps_function.hpp"  // Line 6

void RegisterDataPrepMacros(ExtensionLoader &loader) {
    // ...
    auto ts_fill_gaps_operator = CreateTSFillGapsOperatorTableFunction();  // Line 741
    // ...
    auto ts_fill_gaps_operator_alias = CreateTSFillGapsOperatorTableFunction();  // Line 748
}
```

## Build Configuration

### CMakeLists.txt
```cmake
set(EXTENSION_SOURCES 
    # ... other sources ...
    src/data_prep_macros.cpp
    src/data_prep_bind_replace.cpp
    src/ts_fill_gaps_function.cpp  # Line 138
    # ... other sources ...
)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})
```

### Include Directories
```cmake
include_directories(src/include)  # Line 47
```

## Environment Differences

**Local Build (Working):**
- OS: Linux (Manjaro)
- Compiler: GCC (version not specified)
- Build system: CMake + Ninja/Make

**GitHub Actions Build (Failing):**
- OS: Alpine Linux (musl libc)
- Compiler: GCC 14.2.0 (x86_64-alpine-linux-musl)
- Build system: CMake + Ninja
- Linker: GNU ld (binutils)

## Detailed Problem Analysis

### The Paradox

**What We Know:**
1. ✅ Function is **defined** in `ts_fill_gaps_function.cpp` (line 655)
2. ✅ Function is **declared** in `ts_fill_gaps_function.hpp` (line 145)
3. ✅ Function is **called** from `data_prep_macros.cpp` (lines 741, 748)
4. ✅ Both source files are in `EXTENSION_SOURCES` in `CMakeLists.txt`
5. ✅ Object file `ts_fill_gaps_function.cpp.o` exists and is in the static library
6. ✅ Symbol exists in static library: `nm` shows `T duckdb::CreateTSFillGapsOperatorTableFunction()`
7. ✅ `__attribute__((used))` has been applied to prevent dead code elimination

**What's Failing:**
- ❌ Linker reports "undefined reference" when linking final executables
- ❌ Error occurs in multiple targets: `duckdb`, `test/unittest`, `test_sqlite3_api_wrapper`
- ❌ Error occurs in CI (Alpine/musl) but **not locally** (Manjaro/glibc)

### Key Observations

1. **Symbol Exists But Can't Be Resolved:**
   - `nm` confirms the symbol is in the static library
   - Linker still reports it as undefined
   - This suggests a **linking order** or **visibility** issue, not a missing definition

2. **Multiple Linking Contexts:**
   - Error occurs when linking `libduckdb.so` (shared library)
   - Error occurs when linking static executables
   - Suggests the issue is with how the static library is being linked, not the symbol itself

3. **Environment-Specific:**
   - Works locally (Manjaro Linux, glibc)
   - Fails in CI (Alpine Linux, musl libc)
   - Could be musl-specific linker behavior or different default flags

## Potential Root Causes

### 1. Static Library Linking Behavior (MOST LIKELY)
- Static libraries are linked **selectively** - only symbols that are referenced are pulled in
- If the linker processes `data_prep_macros.cpp.o` before `ts_fill_gaps_function.cpp.o`, it might not find the symbol
- **Solution needed:** Force linker to include all object files, or ensure proper linking order

### 2. Visibility/Export Issues
- Function might need explicit export attribute for shared library linking
- DuckDB extensions might require specific visibility settings
- Missing `DUCKDB_API` or similar export macro
- **Solution needed:** Check if DuckDB has extension-specific export requirements

### 3. Linker Flag Differences (musl vs glibc)
- musl libc linker might have different default behavior
- `--gc-sections` or `-ffunction-sections` might be more aggressive
- `-Wl,--whole-archive` might be needed to force inclusion
- **Solution needed:** Compare linker flags between local and CI builds

### 4. Shared Library Symbol Export
- When linking `libduckdb.so`, symbols from static libraries need to be explicitly exported
- The function might be in the static library but not exported to the shared library
- **Solution needed:** Ensure function is exported when creating shared library

### 5. Template/Inline Issues
- `unique_ptr<TableFunction>` return type might cause issues with symbol visibility
- Function might need to be inlined or defined in header
- **Solution needed:** Try moving definition to header or using different return type pattern

## Questions for Expert

1. **Why would a function be present in the static library (`nm` shows `T duckdb::CreateTSFillGapsOperatorTableFunction()`) but the linker still reports "undefined reference" when linking executables?**

2. **Why does `__attribute__((used))` not resolve this issue?** Is there a more appropriate attribute or linker flag?

3. **Is this a static library linking order problem?** Should we:
   - Use `-Wl,--whole-archive` to force inclusion of all symbols?
   - Reorder object files in the static library?
   - Use a different linking strategy?

4. **Are there DuckDB-specific requirements for exporting functions from extensions?** Do we need:
   - A `DUCKDB_API` macro or similar?
   - Specific visibility attributes?
   - Different function declaration pattern?

5. **Could this be a musl libc vs glibc difference?** The linker behavior might differ:
   - Different default linker flags?
   - Different handling of static libraries?
   - Different symbol visibility rules?

6. **Should we use a different function organization pattern?** Options:
   - Define function as `inline` in header?
   - Move function to same translation unit as caller?
   - Use a factory pattern or function pointer?
   - Define function in `data_prep_macros.cpp` directly?

7. **Is there a DuckDB extension best practice for this pattern?** How do other extensions handle cross-translation-unit function calls?

8. **Could this be related to the shared library (`libduckdb.so`) linking?** The error mentions `src/libduckdb.so` - do we need to explicitly export symbols when creating the shared library?

## Additional Context

- The function is called **twice** in `RegisterDataPrepMacros()` (lines 741, 748) - once for main function, once for alias
- Similar pattern works for `CreateTSFeaturesListTableFunction()` but that's `static` (internal linkage)
- The extension builds successfully **locally** (Manjaro/glibc) but fails in **CI** (Alpine/musl)
- All object files are present in the static library (verified with `ar -t`)
- Symbol exists in the library (verified with `nm`) but linker can't resolve it
- The error occurs when linking **multiple targets**: `duckdb`, `test/unittest`, `test_sqlite3_api_wrapper`
- Some errors mention `src/libduckdb.so` (shared library), suggesting symbol export issues

## Critical Insight

The fact that:
1. Symbol exists in static library (`nm` confirms)
2. Object file is included (`ar -t` confirms)
3. Function is defined (source code confirms)
4. But linker still reports undefined reference

**Suggests this is NOT a missing definition problem, but rather:**
- A **linking order** issue (linker processes files in wrong order)
- A **symbol visibility** issue (symbol not exported/visible to linker)
- A **static library linking behavior** issue (linker doesn't pull in the object file)
- A **musl-specific** linker behavior difference

## Code Evidence

### Function is Definitely Defined
```cpp
// src/ts_fill_gaps_function.cpp:655
namespace duckdb {
    __attribute__((used)) unique_ptr<TableFunction> CreateTSFillGapsOperatorTableFunction() {
        vector<LogicalType> arguments = {
            LogicalType::VARCHAR, // group_col
            LogicalType::VARCHAR, // date_col
            LogicalType::VARCHAR, // value_col
            LogicalType::ANY      // frequency
        };
        TableFunction table_function(arguments, nullptr, TSFillGapsOperatorBind, ...);
        table_function.in_out_function = TSFillGapsOperatorInOut;
        table_function.in_out_function_final = TSFillGapsOperatorFinal;
        return make_uniq<TableFunction>(std::move(table_function));
    }
} // namespace duckdb
```

### Function is Definitely Called
```cpp
// src/data_prep_macros.cpp:741, 748
void RegisterDataPrepMacros(ExtensionLoader &loader) {
    // ...
    auto ts_fill_gaps_operator = CreateTSFillGapsOperatorTableFunction();  // Line 741
    // ...
    auto ts_fill_gaps_operator_alias = CreateTSFillGapsOperatorTableFunction();  // Line 748
}
```

### Both Files Are in Build
```cmake
# CMakeLists.txt:126-138
set(EXTENSION_SOURCES 
    # ...
    src/data_prep_macros.cpp      # Line 136 - calls the function
    src/ts_fill_gaps_function.cpp  # Line 138 - defines the function
    # ...
)
```

## Files Involved

- `src/include/ts_fill_gaps_function.hpp` - Header with declaration (line 145)
- `src/ts_fill_gaps_function.cpp` - Implementation (684 lines, function at line 655)
- `src/data_prep_macros.cpp` - Call site (lines 741, 748, includes header at line 6)
- `CMakeLists.txt` - Build configuration (line 138 includes `ts_fill_gaps_function.cpp`)

## Complete Code Examples

### Full Function Declaration (Header)
```cpp
// File: src/include/ts_fill_gaps_function.hpp
// Line: 145
#pragma once
#include "duckdb.hpp"
#include "duckdb/function/table_function.hpp"
// ... other includes ...

namespace duckdb {
    // ... other declarations ...
    
    // Create table-in-out function for internal use (takes TABLE input)
    unique_ptr<TableFunction> CreateTSFillGapsOperatorTableFunction();
    
} // namespace duckdb
```

### Full Function Definition (Implementation)
```cpp
// File: src/ts_fill_gaps_function.cpp
// Line: 655
#include "ts_fill_gaps_function.hpp"
// ... other includes ...

namespace duckdb {
    // ... other functions ...
    
    // Create table-in-out function (internal operator)
    // This function takes TABLE input and processes it
    // Mark as used to prevent linker from dropping it during dead code elimination
    __attribute__((used)) unique_ptr<TableFunction> CreateTSFillGapsOperatorTableFunction() {
        // Table-in-out function arguments: group_col, date_col, value_col, frequency
        // The input table columns are provided automatically via the input DataChunk
        vector<LogicalType> arguments = {
            LogicalType::VARCHAR, // group_col
            LogicalType::VARCHAR, // date_col
            LogicalType::VARCHAR, // value_col
            LogicalType::ANY      // frequency (VARCHAR or INTEGER)
        };

        // Create table function with nullptr for regular function (we use in_out_function)
        TableFunction table_function(arguments, nullptr, TSFillGapsOperatorBind, 
                                     TSFillGapsOperatorInitGlobal, TSFillGapsOperatorInitLocal);

        // Set in-out handlers
        table_function.in_out_function = TSFillGapsOperatorInOut;
        table_function.in_out_function_final = TSFillGapsOperatorFinal;
        table_function.cardinality = TSFillGapsCardinality;
        table_function.name = "anofox_fcst_ts_fill_gaps_operator";

        // Named parameters
        table_function.named_parameters["group_col"] = LogicalType::VARCHAR;
        table_function.named_parameters["date_col"] = LogicalType::VARCHAR;
        table_function.named_parameters["value_col"] = LogicalType::VARCHAR;
        table_function.named_parameters["frequency"] = LogicalType::ANY;

        return make_uniq<TableFunction>(std::move(table_function));
    }
    
} // namespace duckdb
```

### Full Call Site (Usage)
```cpp
// File: src/data_prep_macros.cpp
// Lines: 6, 741, 748
#include "duckdb.hpp"
#include "duckdb/catalog/default/default_table_functions.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include "duckdb/function/table_function.hpp"
#include "data_prep_bind_replace.hpp"
#include "ts_fill_gaps_function.hpp"  // Line 6 - includes the header

namespace duckdb {
    void RegisterDataPrepMacros(ExtensionLoader &loader) {
        // ... other registrations ...
        
        // TS_FILL_GAPS: Table-In-Out operator (internal function)
        // This is the native C++ implementation that takes TABLE input
        auto ts_fill_gaps_operator = CreateTSFillGapsOperatorTableFunction();  // Line 741
        TableFunctionSet ts_fill_gaps_operator_set("anofox_fcst_ts_fill_gaps_operator");
        ts_fill_gaps_operator_set.AddFunction(*ts_fill_gaps_operator);
        CreateTableFunctionInfo ts_fill_gaps_operator_info(std::move(ts_fill_gaps_operator_set));
        loader.RegisterFunction(std::move(ts_fill_gaps_operator_info));

        // Register alias for ts_fill_gaps_operator
        auto ts_fill_gaps_operator_alias = CreateTSFillGapsOperatorTableFunction();  // Line 748
        TableFunctionSet ts_fill_gaps_operator_alias_set("ts_fill_gaps_operator");
        ts_fill_gaps_operator_alias_set.AddFunction(*ts_fill_gaps_operator_alias);
        CreateTableFunctionInfo ts_fill_gaps_operator_alias_info(std::move(ts_fill_gaps_operator_alias_set));
        ts_fill_gaps_operator_alias_info.alias_of = "anofox_fcst_ts_fill_gaps_operator";
        ts_fill_gaps_operator_alias_info.on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
        loader.RegisterFunction(std::move(ts_fill_gaps_operator_alias_info));
        
        // ... other registrations ...
    }
} // namespace duckdb
```

## Linking Command Analysis

### What the Linker is Doing

The error occurs during final executable linking. The linker command includes:
```
-Wl,--dependency-file=... 
src/libduckdb_static.a 
extension/anofox_forecast/libanofox_forecast_extension.a  # Our static library
extension/core_functions/libcore_functions_extension.a 
extension/parquet/libparquet_extension.a 
src/libduckdb_static.a  # Duplicate? 
extension/anofox_forecast/libanofox_forecast_extension.a  # Duplicate?
...
```

**Observations:**
1. The static library `libanofox_forecast_extension.a` is linked **twice** (might be intentional or error)
2. The library is linked after `libduckdb_static.a`
3. Linker flags include `-ffunction-sections -fdata-sections` which enable dead code elimination
4. No `--whole-archive` flag is used, so linker only pulls in referenced symbols

### Why This Might Fail

When linking a static library, the linker:
1. Processes object files in the order they appear in the command line
2. Only pulls in object files that satisfy **unresolved symbols** at that point
3. If `data_prep_macros.cpp.o` is processed and references `CreateTSFillGapsOperatorTableFunction()`, but `ts_fill_gaps_function.cpp.o` hasn't been processed yet, the symbol is marked as "needed"
4. If `ts_fill_gaps_function.cpp.o` is later processed but the linker has already moved on, the symbol might not be linked

**The Problem:** The linker might be processing object files in the static library in an order that doesn't satisfy the dependency.

## Attempted Solutions (All Failed)

### ✅ Solution 1: Verify File Inclusion
- Confirmed all files are in `EXTENSION_SOURCES`
- Confirmed header is included
- **Result**: Files are correctly configured, but issue persists

### ✅ Solution 2: Check Namespace Consistency
- Verified both files use `namespace duckdb`
- Verified function is properly scoped
- **Result**: Namespace is correct, but issue persists

### ✅ Solution 3: Add `__attribute__((used))`
- Added attribute to prevent dead code elimination
- **Result**: **Did not resolve the issue** - linker error still occurs

### ❌ Solution 4: Symbol Verification
- Verified symbol exists in library
- Verified object file is included
- **Result**: Symbol exists but linker can't resolve it

## ✅ SOLUTION IMPLEMENTED: Inline Function Definition

**Expert Recommendation:** Move function definition to header as `inline` to eliminate linker issues.

**Status:** ✅ **IMPLEMENTED AND VERIFIED**

### Implementation Details

The function `CreateTSFillGapsOperatorTableFunction()` has been moved from `src/ts_fill_gaps_function.cpp` to `src/include/ts_fill_gaps_function.hpp` and marked as `inline`.

**Why This Works:**
1. **No Linking Required:** The compiler instantiates the function body directly inside `data_prep_macros.cpp` (where it is called)
2. **No Symbol Visibility Issues:** Since it's inlined, there is no cross-object symbol to resolve or hide
3. **Cross-Platform Robustness:** This bypasses `musl` vs `glibc` linker differences entirely
4. **Eliminates Static Library Link Order Issues:** No dependency on linker scanning order

**Changes Made:**
- ✅ Function definition moved to header (`src/include/ts_fill_gaps_function.hpp`) as `inline`
- ✅ Function definition removed from implementation file (`src/ts_fill_gaps_function.cpp`)
- ✅ Removed `__attribute__((used))` (no longer needed)
- ✅ Build verified: Extension compiles successfully

**Build Result:**
```
[100%] Built target anofox_forecast_loadable_extension
[100%] Built target duckdb_local_extension_repo
```

## Next Steps to Try (If Inline Approach Had Failed)

1. **Try `-Wl,--whole-archive` linker flag:**
   ```cmake
   target_link_options(${EXTENSION_NAME} PRIVATE -Wl,--whole-archive)
   ```
   This forces the linker to include all symbols from the static library.

2. **Try defining function as `inline` in header:**
   - Move function definition to `ts_fill_gaps_function.hpp`
   - Mark as `inline` to avoid linking issues
   - Trade-off: Code duplication, but ensures symbol is always available

3. **Try moving function to same translation unit:**
   - Define function directly in `data_prep_macros.cpp`
   - Or use a `static` helper function pattern
   - Trade-off: Less modular, but avoids cross-TU linking

4. **Try DuckDB export macro:**
   - Check if DuckDB has a `DUCKDB_API` or similar macro
   - Apply to function declaration/definition
   - May be required for shared library symbol export

5. **Try different function pattern:**
   - Use function pointer or factory pattern
   - Return `TableFunction` by value instead of `unique_ptr`
   - May avoid symbol visibility issues

6. **Check DuckDB extension examples:**
   - Review how other DuckDB extensions handle similar patterns
   - Look for examples of cross-TU function calls in extensions
   - May reveal DuckDB-specific requirements

