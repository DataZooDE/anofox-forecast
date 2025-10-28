# Installation Guide

## Prerequisites

### Required Dependencies

1. **C++ Compiler** (GCC 9+ or Clang 10+)
2. **CMake** (3.15+)
3. **Make** or **Ninja**
4. **OpenSSL** (development libraries)
5. **Eigen3** (header-only linear algebra library)

### Install Dependencies

#### Manjaro/Arch Linux (your system)

```bash
sudo pacman -S base-devel cmake ninja openssl eigen
```

#### Ubuntu/Debian

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build libssl-dev libeigen3-dev
```

#### Fedora/RHEL

```bash
sudo dnf install gcc-c++ cmake ninja-build openssl-devel eigen3-devel
```

#### macOS

```bash
brew install cmake ninja openssl eigen
```

#### Windows

**Option 1: vcpkg (Recommended for Visual Studio)**

```powershell
# Install Visual Studio 2019 or later with C++ workload
# https://visualstudio.microsoft.com/downloads/

# Clone and bootstrap vcpkg
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat

# Install dependencies
.\vcpkg install eigen3:x64-windows
.\vcpkg install openssl:x64-windows

# Return to project directory
cd ..\anofox-forecast

# Configure with vcpkg
cmake -DCMAKE_TOOLCHAIN_FILE=..\vcpkg\scripts\buildsystems\vcpkg.cmake -A x64 .

# Build
cmake --build . --config Release
```

**Option 2: MSYS2/MinGW (Unix-like on Windows)**

```bash
# Download and install MSYS2 from https://www.msys2.org/

# Open MSYS2 MinGW64 terminal
pacman -Syu  # Update package database

# Install build tools
pacman -S mingw-w64-x86_64-gcc
pacman -S mingw-w64-x86_64-cmake
pacman -S mingw-w64-x86_64-ninja
pacman -S mingw-w64-x86_64-make

# Install dependencies
pacman -S mingw-w64-x86_64-openssl
pacman -S mingw-w64-x86_64-eigen3

# Clone and build (in MSYS2 terminal)
git clone --recurse-submodules https://github.com/DataZooDE/anofox-forecast.git
cd anofox-forecast
make -j$(nproc)
```

**Option 3: WSL (Ubuntu on Windows, Easiest)**

```powershell
# Install WSL from PowerShell (admin)
wsl --install

# Restart computer
# Open Ubuntu from Start Menu

# Follow Ubuntu instructions
sudo apt update
sudo apt install build-essential cmake ninja-build libssl-dev libeigen3-dev

git clone --recurse-submodules https://github.com/DataZooDE/anofox-forecast.git
cd anofox-forecast
make -j$(nproc)
```

## Build Instructions

### 1. Clone Repository

```bash
git clone --recurse-submodules https://github.com/DataZooDE/anofox-forecast.git
cd anofox-forecast
```

**Note**: The `--recurse-submodules` flag is important to get DuckDB and extension-ci-tools.

### 2. Build with Make

```bash
# Default build (uses Make)
make -j$(nproc)
```

### 3. Build with Ninja (faster)

```bash
# Use Ninja generator
GEN=ninja make release
```

### 4. Build Types

**Release build** (optimized, recommended):
```bash
make release -j$(nproc)
```

**Debug build** (with debug symbols):
```bash
make debug -j$(nproc)
```

**Clean build**:
```bash
make clean
make -j$(nproc)
```

## Verify Installation

### Test the Extension

```bash
# Run DuckDB with the extension
./build/release/duckdb

# In DuckDB prompt:
LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- Test a simple forecast
CREATE TABLE test AS
SELECT DATE '2023-01-01' + INTERVAL (d) DAY AS date,
       100 + 20 * SIN(2 * PI() * d / 7) AS amount
FROM generate_series(0, 89) t(d);

SELECT * FROM TS_FORECAST('test', date, amount, 'AutoETS', 7, {'seasonal_period': 7});

-- Should show forecast results!
```

## Common Issues

### Issue 1: Eigen3 Not Found

**Error**:
```
CMake Error: Could not find a package configuration file provided by "Eigen3"
```

**Solution** (Manjaro/Arch):
```bash
sudo pacman -S eigen
```

**Solution** (Ubuntu/Debian):
```bash
sudo apt install libeigen3-dev
```

**Solution** (macOS):
```bash
brew install eigen
```

**Verify installation**:
```bash
# Check if Eigen3 is installed
ls /usr/include/eigen3/  # Linux
ls /usr/local/include/eigen3/  # macOS (Homebrew)
```

### Issue 2: OpenSSL Not Found

**Error**:
```
Could not find a package configuration file provided by "OpenSSL"
```

**Solution** (Manjaro/Arch):
```bash
sudo pacman -S openssl
```

**Solution** (Ubuntu/Debian):
```bash
sudo apt install libssl-dev
```

### Issue 3: Submodules Not Initialized

**Error**:
```
fatal: not a git repository (or any of the parent directories): .git
```

**Solution**:
```bash
# Initialize submodules
git submodule update --init --recursive
```

### Issue 4: Build Permission Denied

**Error**:
```
Permission denied
```

**Solution**:
```bash
# Make build script executable
chmod +x build.sh

# Or use make directly
make -j$(nproc)
```

### Issue 5: Out of Memory

**Error**:
```
c++: fatal error: Killed signal terminated program cc1plus
```

**Solution**:
```bash
# Reduce parallel jobs
make -j2  # Use only 2 cores instead of all

# Or add swap space
sudo fallocate -l 4G /swapfile
sudo chmod 600 /swapfile
sudo mkswap /swapfile
sudo swapon /swapfile
```

## Advanced Build Options

### Custom Install Location

```bash
# Install to custom directory
cmake -DCMAKE_INSTALL_PREFIX=/path/to/install .
make install
```

### With vcpkg (Alternative Dependency Management)

```bash
# Clone vcpkg
git clone https://github.com/Microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh

# Install dependencies
./vcpkg/vcpkg install eigen3 openssl

# Build with vcpkg
cmake -DCMAKE_TOOLCHAIN_FILE=./vcpkg/scripts/buildsystems/vcpkg.cmake .
make -j$(nproc)
```

### Enable AVX2 Optimizations

AVX2 is auto-detected. To force or disable:

```bash
# Force enable AVX2
cmake -DFORCE_AVX2=ON .
make -j$(nproc)

# Disable AVX2
cmake -DDISABLE_AVX2=ON .
make -j$(nproc)
```

## Post-Installation

### Add to DuckDB Extension Directory

```bash
# Copy extension to DuckDB's extension directory
mkdir -p ~/.duckdb/extensions/v1.4.1/linux_amd64/
cp build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension \
   ~/.duckdb/extensions/v1.4.1/linux_amd64/

# Now can load without path
duckdb
> INSTALL anofox_forecast;
> LOAD anofox_forecast;
```

### System-Wide Installation

```bash
# Install to /usr/local
sudo mkdir -p /usr/local/lib/duckdb/extensions
sudo cp build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension \
        /usr/local/lib/duckdb/extensions/
```

## Development Setup

### For Extension Development

```bash
# Clone
git clone --recurse-submodules https://github.com/DataZooDE/anofox-forecast.git
cd anofox-forecast

# Install dependencies
sudo pacman -S base-devel cmake ninja openssl eigen  # Manjaro
# OR
sudo apt install build-essential cmake ninja-build libssl-dev libeigen3-dev  # Ubuntu

# Build in debug mode
make debug

# Run tests
make test

# Development workflow
# 1. Edit code
# 2. make -j$(nproc)
# 3. Test: ./build/release/duckdb < test/sql/test.sql
```

### IDE Setup (VS Code / CLion)

```bash
# Generate compile_commands.json for IDE
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON .

# VS Code: Install C/C++ extension, it will auto-detect compile_commands.json
# CLion: File → Open → Select CMakeLists.txt
```

## Quick Installation (TL;DR)

### Manjaro/Arch (Your System)

```bash
# 1. Install dependencies
sudo pacman -S base-devel cmake ninja openssl eigen

# 2. Clone and build
git clone --recurse-submodules https://github.com/DataZooDE/anofox-forecast.git
cd anofox-forecast
GEN=ninja make release

# 3. Test
./build/release/duckdb -c "
LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';
SELECT 'Extension loaded successfully!' AS status;
"
```

**That's it!** ✅

---

## Troubleshooting

Still having issues? Check:

1. **CMake version**: `cmake --version` (need 3.15+)
2. **Eigen3 installed**: `pacman -Q eigen` (should show version)
3. **OpenSSL installed**: `pacman -Q openssl` (should show version)
4. **Submodules initialized**: `ls duckdb/` (should have files, not empty)
5. **Disk space**: `df -h` (need ~5GB for build)

**Get help**:
- GitHub Issues: https://github.com/DataZooDE/anofox-forecast/issues
- Email: support@anofox.com

---

**Next**: After installation, see [Quick Start Guide](guides/01_quickstart.md) to generate your first forecast!

