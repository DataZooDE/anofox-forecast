# THIRD PARTY NOTICES AND LICENSES

The Anofox Forecast Extension for DuckDB (Rust Port) incorporates material from the
projects listed below. The original copyright notices and licenses under which
DataZoo GmbH received such material are set forth below.

---

## anofox-forecast (Rust Crate)

   <https://crates.io/crates/anofox-forecast>

   The core forecasting library providing time series forecasting algorithms.

   Licensed under the Apache License, Version 2.0.

   Components used:
   - ETS state-space models and AutoETS (`crates/anofox-fcst-core/src/forecast.rs`)
   - Theta methods and AutoTheta (`crates/anofox-fcst-core/src/forecast.rs`)
   - MFLES boosting and AutoMFLES (`crates/anofox-fcst-core/src/forecast.rs`)
   - MSTL decomposition (`crates/anofox-fcst-core/src/decomposition.rs`)
   - Intermittent demand models (ADIDA, Croston, IMAPA, TSB)
   - ARIMA and AutoARIMA implementations
   - Changepoint detection (PELT, BOCPD) (`crates/anofox-fcst-core/src/changepoint.rs`)

---

## StatsForecast (Algorithm Reference)

   <https://github.com/Nixtla/statsforecast>

   Copyright 2022 Nixtla

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

   The forecasting algorithms in this extension implement the same published
   algorithms described in StatsForecast, with parameter sets aligned to produce
   compatible results.

---

## statrs

   <https://github.com/statrs-dev/statrs>

   Statistical computation library for Rust.

   The MIT License (MIT)

   Copyright (c) 2016 Michael Ma

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.

---

## chrono

   <https://github.com/chronotope/chrono>

   Date and time library for Rust.

   Dual-licensed under Apache 2.0 and MIT licenses.

   Copyright (c) 2014, Kang Seonghoon and contributors.

   Components used:
   - Date/time parsing and formatting (`crates/anofox-fcst-core/src/`)
   - Timestamp handling for time series data

---

## tsfresh (Algorithm Reference)

   <https://github.com/blue-yonder/tsfresh>

   The MIT License

   Copyright (c) blue-yonder/tsfresh contributors

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.

   The time series feature extraction in `crates/anofox-fcst-core/src/features.rs`
   implements algorithms compatible with tsfresh feature definitions.

---

## thiserror

   <https://github.com/dtolnay/thiserror>

   Derive macro for the standard library's std::error::Error trait.

   Dual-licensed under Apache 2.0 and MIT licenses.

   Copyright (c) David Tolnay

---

## libc

   <https://github.com/rust-lang/libc>

   Raw FFI bindings to platform libraries.

   Dual-licensed under Apache 2.0 and MIT licenses.

   Copyright (c) The Rust Project Developers

