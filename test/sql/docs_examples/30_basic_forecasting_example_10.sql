{
    'seasonal_period': INT,        -- REQUIRED for seasonal models
                                   -- 7=weekly, 30=monthly, 365=yearly
    
    'confidence_level': DOUBLE,    -- Default: 0.90 (90% CI)
                                   -- Common: 0.80, 0.90, 0.95, 0.99
    
    'return_insample': BOOLEAN     -- Default: false
                                   -- true = get fitted values for diagnostics
}
