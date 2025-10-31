-- ERROR: Arrays must have same length
SELECT TS_MAE([1, 2, 3], [1, 2]);

-- ERROR: Arrays must not be empty
SELECT TS_MAE([], []);

-- ERROR: MAPE undefined for zeros
SELECT TS_MAPE([0, 1, 2], [0, 1, 2]);
-- Returns NULL (gracefully handled)

-- ERROR: MASE requires 3 arguments
SELECT TS_MASE([1, 2], [1, 2]);
-- Use: TS_MASE([1, 2], [1, 2], baseline)
