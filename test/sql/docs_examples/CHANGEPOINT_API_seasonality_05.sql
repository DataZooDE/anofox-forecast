-- Highly sensitive: detect even small changes
{'hazard_lambda': 50.0}

-- Default: balanced detection
MAP{}  -- or {'hazard_lambda': 250.0}

-- Conservative: only major shifts
{'hazard_lambda': 500.0}
