# Linker Error: Undefined Reference to CreateTSFillGapsOperatorTableFunction()

## Issue Summary

**Error Message:**
```
/usr/lib/gcc/x86_64-alpine-linux-musl/14.2.0/../../../../x86_64-alpine-linux-musl/bin/ld: 
extension/anofox_forecast/libanofox_forecast_extension.a(data_prep_macros.cpp.o): 
in function `duckdb::RegisterDataPrepMacros(duckdb::ExtensionLoader&)':
data_prep_macros.cpp:(.text._ZN6duckdb22RegisterDataPrepMacrosERNS_15ExtensionLoaderE+0x1518): 
undefined reference to `duckdb::CreateTSFillGapsOperatorTableFunction()'
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
- **Status**: Attempted but build still in progress

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

## Potential Root Causes

### 1. Linker Dead Code Elimination
- The linker might be removing the function during optimization
- `__attribute__((used))` should prevent this, but may not be sufficient
- Static library linking order might be causing issues

### 2. Visibility/Export Issues
- Function might need explicit export attribute
- DuckDB extensions might require specific visibility settings
- Missing `DUCKDB_API` or similar export macro

### 3. Static Library Linking Order
- The order of object files in the static library might matter
- Linker might not be resolving symbols correctly
- Need to ensure `ts_fill_gaps_function.cpp.o` is linked before `data_prep_macros.cpp.o`

### 4. Compiler/Linker Flags
- Different optimization levels between local and CI
- Missing `-fno-common` or similar flags
- Link-time optimization (LTO) might be interfering

### 5. Template/Inline Issues
- `unique_ptr<TableFunction>` return type might cause issues
- Function might need to be inlined or templated differently

## Questions for Expert

1. **Why would a function be present in the static library (`nm` shows it) but still cause an undefined reference error?**

2. **Is `__attribute__((used))` sufficient, or do we need additional linker flags or attributes?**

3. **Should we use a different approach, such as:**
   - Defining the function as `static inline` in the header?
   - Using a factory pattern instead?
   - Moving the function definition to the same translation unit?

4. **Are there DuckDB-specific requirements for exporting functions from extensions that we're missing?**

5. **Could this be a musl libc vs glibc difference in how static libraries are handled?**

6. **Should we use `-Wl,--whole-archive` or similar linker flags to ensure all symbols are included?**

## Additional Context

- The function is called twice in `RegisterDataPrepMacros()` (for main function and alias)
- Similar pattern works for `CreateTSFeaturesListTableFunction()` but that's `static`
- The extension builds successfully locally but fails in CI
- All object files are present in the static library
- Symbol exists in the library but linker can't resolve it

## Files Involved

- `src/include/ts_fill_gaps_function.hpp` - Header with declaration
- `src/ts_fill_gaps_function.cpp` - Implementation (655 lines)
- `src/data_prep_macros.cpp` - Call site (lines 741, 748)
- `CMakeLists.txt` - Build configuration (line 138)

## Next Steps (If Current Fix Fails)

1. Try defining function as `inline` in header
2. Try using `-Wl,--whole-archive` linker flag
3. Try moving function definition to header file
4. Try using a different function registration pattern
5. Check if DuckDB has specific export requirements

