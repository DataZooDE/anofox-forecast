# SQL micro-benchmark comparison

| label | median v1.5.5 s | median 2.0 s | delta % | baseline drift % (run1 vs run2) | checksum match |
|---|---|---|---|---|---|
| cv_folds | 0.287 | 0.507 | +76.7 | -0.7 | yes |
| cv_forecast | 24.492 | 39.846 | +62.7 | +0.5 | yes (v1.5.5 vs v2.0 checksum differs — see Findings) |
| features_by | 13.080 | 23.849 | +82.3 | +0.1 | yes (v1.5.5 vs v2.0 checksum differs — see Findings) |
| fill_gaps | 0.090 | 0.149 | +65.6 | +4.4 | yes (v1.5.5 vs v2.0 checksum differs — see Findings) |
| forecast_autoets | 1.292 | 3.133 | +142.5 | -9.8 | yes (v1.5.5 vs v2.0 checksum differs — see Findings) |
| forecast_mfles | 0.163 | 0.375 | +130.1 | +9.2 | yes (v1.5.5 vs v2.0 checksum differs — see Findings) |
| forecast_naive | 0.102 | 0.431 | +322.5 | +26.5 | yes (v1.5.5 vs v2.0 checksum differs — see Findings) |
| forecast_theta | 0.117 | 0.249 | +112.8 | +6.8 | yes (v1.5.5 vs v2.0 checksum differs — see Findings) |
| stats_by | 0.146 | 0.208 | +42.5 | -12.3 | yes (v1.5.5 vs v2.0 checksum differs — see Findings) |
