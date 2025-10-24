#!/usr/bin/env python3
"""
Simple check of statsforecast MNM parameters
"""

import numpy as np
from statsforecast.ets import ets_f

# AirPassengers data (first 132 observations)
data = np.array([112, 118, 132, 129, 121, 135, 148, 148, 136, 119, 104, 118,
                 115, 126, 141, 135, 125, 149, 170, 170, 158, 133, 114, 140,
                 145, 150, 178, 163, 172, 178, 199, 199, 184, 162, 146, 166,
                 171, 180, 193, 181, 183, 218, 230, 242, 209, 191, 172, 194,
                 196, 196, 236, 235, 229, 243, 264, 272, 237, 211, 180, 201,
                 204, 188, 235, 227, 234, 264, 302, 293, 259, 229, 203, 229,
                 242, 233, 267, 269, 270, 315, 364, 347, 312, 274, 237, 278,
                 284, 277, 317, 313, 318, 374, 413, 405, 355, 306, 271, 306,
                 315, 301, 356, 348, 355, 422, 465, 467, 404, 347, 305, 336,
                 340, 318, 362, 348, 363, 435, 491, 505, 404, 359, 310, 337,
                 360, 342, 406, 396, 420, 472, 548, 559, 463, 407, 362, 405], dtype=np.float64)

print("Testing statsforecast ets_f with MNM model")
print("=" * 70)

model = ets_f(data, m=12, model='MNM', damped=False, phi=None)

print(f"\nModel parameters:")
if 'par' in model:
    par = model['par']
    if isinstance(par, dict):
        print(f"  alpha: {par.get('alpha', 'N/A')}")
        print(f"  gamma: {par.get('gamma', 'N/A')}")
    elif isinstance(par, np.ndarray):
        print(f"  Parameters array: {par}")
    else:
        print(f"  Parameters: {par}")
    
if 'states' in model:
    states = model['states']
    print(f"\nStates shape: {states.shape}")
    print(f"Final states (last row):")
    print(f"  Level (l): {states[-1, 0]:.4f}")
    
    # Seasonal components start from index 1
    if states.shape[1] > 12:
        print(f"\nSeasonal components (s[1] to s[12]):")
        for i in range(1, 13):
            print(f"    s[{i:2d}] = {states[-1, i]:.6f}")

print(f"\nForecast test:")
from statsforecast.ets import forecast_ets
fcst = forecast_ets(model, h=12, level=None)
print(f"  First 3 forecasts: {fcst['mean'][:3]}")
print(f"  Expected: [407, 402, 456]")
print(f"  Error: {np.abs((fcst['mean'][:3] - [407, 402, 456]) / [407, 402, 456] * 100).max():.2f}%")

