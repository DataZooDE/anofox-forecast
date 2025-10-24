# ETS Complete Rewrite - Based on statsforecast C++ Core

## Critical Findings

### 1. Seasonal Array Management (ROOT CAUSE)
**statsforecast** uses a **rotating buffer**:
- `s[0]` = newest seasonal (just computed)
- `s[m-1]` = oldest seasonal (from m steps ago)
- Each Update rotates: `s[1..m-1] = old_s[0..m-2]`

**Our current code** uses **time-indexed**:
- `seasonals_[t % m]` = seasonal for time t
- Updates in-place at current time index

This is a **fundamental architectural difference** that causes wrong fitted states!

### 2. Update Function Exact Logic (ets_cpp.txt lines 25-89)

```cpp
// Step 1: Compute one-step forecast q
q = old_l + phi * old_b  // (additive trend)

// Step 2: De-seasonalize observation  
p = y - old_s[m-1]  // (additive seasonal)

// Step 3: New level
l = q + alpha * (p - q)

// Step 4: New trend
r = l - old_l  // Change in level
b = phi*old_b + (beta/alpha) * (r - phi*old_b)

// Step 5: New seasonal
t = y - q  // Uses ORIGINAL q, not new level!
s[0] = old_s[m-1] + gamma * (t - old_s[m-1])

// Step 6: Rotate seasonal array
s[1..m-1] = old_s[0..m-2]
```

### 3. Forecast Function (ets_cpp.txt lines 92-122)

```cpp
for (int i = 0; i < h; ++i) {
    forecast[i] = l + phistar * b;
    
    // Seasonal indexing: BACKWARDS from end
    j = m - 1 - i;
    while (j < 0) j += m;
    forecast[i] += s[j];
    
    // Accumulate phistar for damping
    phistar += pow(phi, i+1);
}
```

## Implementation Status

### ✅ Completed
1. Created exact port of statsforecast Update/Forecast in `ets_core_statsforecast.cpp`
2. Fixed seasonal indexing in Forecast (backwards from m-1)
3. Fixed initparam defaults (alpha, beta, gamma, phi)
4. AutoETS now selects damped models with correct AICc (1410 vs 1409)

### 🔧 In Progress  
1. Integrating statsforecast Update into ETS::fitInternal
   - Need to replace entire update loop (lines 371-523)
   - Manage rotating seasonal buffer
   - Compute fitted/residuals/innovations correctly

2. Integrating statsforecast Forecast into ETS::predict
   - Replace computeForecastComponent with direct ets_forecast_statsforecast call
   - Use rotating seasonal buffer correctly

### 📋 TODO
1. Test with exact statsforecast parameters:
   ```
   alpha=0.258, beta=0.0075, gamma=0.735, phi=0.888
   Expected: l=397.08, b=0.86, s[0]=7.77
   ```

2. Validate all ETS model combinations (30 total):
   - Error: A, M
   - Trend: N, A, Ad, M, Md  (5)
   - Season: N, A, M  (3)
   - Total: 2 × 5 × 3 = 30 models

3. Achieve <1% error for HoltWinters/AutoETS

## Key Files Modified
- `anofox-time/src/models/ets_core_statsforecast.cpp` - Exact port ✅
- `anofox-time/src/models/ets.cpp` - Integration (in progress)
- `anofox-time/src/models/auto_ets.cpp` - initparam fixes ✅

## Next Steps
1. Complete fitInternal rewrite to use ets_update_statsforecast
2. Complete predict rewrite to use ets_forecast_statsforecast  
3. Add rotating seasonal buffer initialization
4. Test and validate against statsforecast

## Estimated Time
- Complete rewrite: 4-6 hours
- Testing/validation: 2-3 hours
- **Total: 6-9 hours of focused work**

