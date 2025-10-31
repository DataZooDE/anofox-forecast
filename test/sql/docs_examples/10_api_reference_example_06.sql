{
    'p': INT,                  -- AR order (0-5 typical)
    'd': INT,                  -- Differencing (0-2 typical)
    'q': INT,                  -- MA order (0-5 typical)
    'P': INT,                  -- Seasonal AR (0-2)
    'D': INT,                  -- Seasonal differencing (0-1)
    'Q': INT,                  -- Seasonal MA (0-2)
    's': INT,                  -- Seasonal period
    'include_intercept': BOOL  -- Include constant term
}
