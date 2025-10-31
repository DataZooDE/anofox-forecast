-- Filter before heavy operations
WITH filtered AS (
    SELECT * FROM sales WHERE product_category = 'Electronics'
)
SELECT * FROM TS_STATS('filtered', ...);
