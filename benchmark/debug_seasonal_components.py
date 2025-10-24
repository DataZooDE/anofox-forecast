#!/usr/bin/env python3
"""
Compare seasonal components between statsforecast and anofox-time
"""

import numpy as np
from statsforecast.models import AutoETS
from statsforecast import StatsForecast

# AirPassengers data (first 120 observations)
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
                 360, 342, 406, 396, 420, 472, 548, 559, 463, 407, 362, 405])

print("=" * 70)
print("Testing MNM (Multiplicative Error, No Trend, Multiplicative Season)")
print("=" * 70)

# Test with statsforecast
try:
    from statsforecast.models import AutoETS
    from statsforecast import StatsForecast
    
    sf = StatsForecast(
        models=[AutoETS(season_length=12, model='MNM')],
        freq='MS'
    )
    result = sf.fit(data)
    fc_sf = sf.forecast(h=12, fitted=True)
    
    print(f"\nstatsforecast MNM:")
    print(f"  Forecast (first 3): {fc_sf['AutoETS'].values[:3]}")
    
    # Try to access model parameters if available
    if hasattr(sf.fitted_[0][0], 'model_'):
        model = sf.fitted_[0][0].model_
        if 'par' in model:
            print(f"  Parameters: {model['par']}")
        if 'states' in model:
            states = model['states']
            print(f"  Final level: {states[-1, 0] if len(states.shape) > 1 else states[-1]}")
            if len(states.shape) > 1 and states.shape[1] > 12:
                print(f"  Seasonal components (last 12):")
                for i in range(12):
                    if states.shape[1] > i + 1:
                        print(f"    s[{i}] = {states[-1, i+1]:.4f}")
    
except Exception as e:
    print(f"\nstatsforecast failed: {e}")
    import traceback
    traceback.print_exc()

print("\n" + "=" * 70)
print("Analysis:")
print("=" * 70)
print("""
The key question: What are the seasonal component values?

For multiplicative seasonality, they should be ratios around 1.0:
  - January (low):  ~0.7-0.8
  - July (high):    ~1.3-1.4
  - December (low): ~0.85-0.95

If all seasonal components are ~1.0 or all the same, that explains constant forecasts.
If they vary properly, the problem is elsewhere.
""")

