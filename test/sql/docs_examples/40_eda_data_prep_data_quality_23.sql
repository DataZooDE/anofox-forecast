-- Only forecast high-quality series
WITH quality_check AS (
    SELECT series_id
    FROM sales_stats
    WHERE quality_score >= 0.7        -- High quality
      AND length >= 60                -- Sufficient history
      AND n_unique_values > 5         -- Not near-constant
      AND intermittency < 0.30        -- Not too sparse
)
SELECT s.*
FROM sales_prepared s
WHERE s.product_id IN (SELECT series_id FROM quality_check);
