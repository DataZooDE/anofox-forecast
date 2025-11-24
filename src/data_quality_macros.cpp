#include "duckdb.hpp"
#include "duckdb/catalog/default/default_table_functions.hpp"

namespace duckdb {

// Array of data quality macros
static const DefaultTableMacro data_quality_macros[] = {

    // TS_DATA_QUALITY_HEALTH_CARD: Comprehensive data quality assessment (with n_short parameter)
    {DEFAULT_SCHEMA,
     "ts_data_quality_health_card",
     {"table_name", "unique_id_col", "date_col", "value_col", "n_short", nullptr},
     {{nullptr, nullptr}},
     R"(
        WITH base_data AS (
            SELECT 
                unique_id_col AS __uid,
                date_col AS __date,
                value_col AS __value
            FROM QUERY_TABLE(table_name)
        ),
        params AS (
            SELECT COALESCE(CAST(n_short AS INTEGER), 30) AS n_short_threshold
        ),
        -- Helper CTEs for structural checks
        key_counts AS (
            SELECT 
                __uid,
                __date,
                COUNT(*) AS key_count
            FROM base_data
            GROUP BY __uid, __date
        ),
        duplicate_stats AS (
            SELECT 
                __uid,
                SUM(CASE WHEN key_count > 1 THEN key_count - 1 ELSE 0 END) AS n_duplicates
            FROM key_counts
            GROUP BY __uid
        ),
        -- Helper CTEs for temporal checks
        series_stats AS (
            SELECT 
                __uid,
                COUNT(*) AS length,
                MIN(__date) AS start_date,
                MAX(__date) AS end_date
            FROM base_data
            GROUP BY __uid
        ),
        series_ranges AS (
            SELECT 
                __uid,
                MIN(__date) AS start_date,
                MAX(__date) AS end_date,
                COUNT(*) AS actual_count
            FROM base_data
            GROUP BY __uid
        ),
        expected_counts AS (
            SELECT 
                __uid,
                start_date,
                end_date,
                actual_count,
                CASE 
                    WHEN end_date >= start_date 
                    THEN CAST(DATEDIFF('day', start_date, end_date) + 1 AS INTEGER)
                    ELSE 1
                END AS expected_count
            FROM series_ranges
        ),
        gap_stats AS (
            SELECT 
                __uid,
                actual_count,
                expected_count,
                expected_count - actual_count AS n_gaps,
                CASE 
                    WHEN expected_count > 0 
                    THEN 100.0 * (expected_count - actual_count) / expected_count
                    ELSE 0.0
                END AS gap_pct
            FROM expected_counts
        ),
        series_bounds AS (
            SELECT 
                __uid,
                MIN(__date) AS start_date,
                MAX(__date) AS end_date
            FROM base_data
            GROUP BY __uid
        ),
        alignment_stats AS (
            SELECT 
                COUNT(DISTINCT start_date) AS n_start_dates,
                COUNT(DISTINCT end_date) AS n_end_dates,
                COUNT(DISTINCT __uid) AS n_series
            FROM series_bounds
        ),
        frequency_stats AS (
            SELECT 
                __uid,
                COUNT(*) AS n_points,
                MIN(__date) AS start_date,
                MAX(__date) AS end_date,
                CASE 
                    WHEN MAX(__date) > MIN(__date)
                    THEN CAST(DATEDIFF('day', MIN(__date), MAX(__date)) AS DOUBLE) / GREATEST(COUNT(*) - 1, 1)
                    ELSE NULL
                END AS avg_interval_days
            FROM base_data
            GROUP BY __uid
        ),
        frequency_classification AS (
            SELECT 
                __uid,
                avg_interval_days,
                CASE 
                    WHEN avg_interval_days IS NULL THEN 'Unknown'
                    WHEN avg_interval_days < 0.5 THEN 'Sub-hourly'
                    WHEN avg_interval_days < 1.0 THEN 'Hourly'
                    WHEN avg_interval_days < 7.0 THEN 'Daily'
                    WHEN avg_interval_days < 30.0 THEN 'Weekly'
                    WHEN avg_interval_days < 90.0 THEN 'Monthly'
                    ELSE 'Quarterly+'
                END AS inferred_frequency
            FROM frequency_stats
        ),
        frequency_diversity AS (
            SELECT 
                COUNT(DISTINCT inferred_frequency) AS n_frequencies,
                COUNT(DISTINCT __uid) AS n_series
            FROM frequency_classification
            WHERE inferred_frequency != 'Unknown'
        ),
        -- Helper CTEs for magnitude checks
        missing_stats AS (
            SELECT 
                __uid,
                COUNT(*) AS total_count,
                COUNT(CASE WHEN __value IS NULL THEN 1 END) AS null_count,
                CASE 
                    WHEN COUNT(*) > 0 
                    THEN 100.0 * COUNT(CASE WHEN __value IS NULL THEN 1 END) / COUNT(*)
                    ELSE 0.0
                END AS null_pct
            FROM base_data
            GROUP BY __uid
        ),
        negative_stats AS (
            SELECT 
                __uid,
                COUNT(CASE WHEN __value < 0 THEN 1 END) AS negative_count,
                COUNT(*) AS total_count
            FROM base_data
            WHERE __value IS NOT NULL
            GROUP BY __uid
        ),
        variance_stats AS (
            SELECT 
                __uid,
                COUNT(*) AS count,
                COUNT(DISTINCT __value) AS distinct_count,
                STDDEV(__value) AS stddev
            FROM base_data
            WHERE __value IS NOT NULL
            GROUP BY __uid
        ),
        -- Helper CTEs for behavioural checks
        zero_stats AS (
            SELECT 
                __uid,
                COUNT(*) AS total_count,
                COUNT(CASE WHEN __value = 0 OR __value IS NULL THEN 1 END) AS zero_count,
                CASE 
                    WHEN COUNT(*) > 0 
                    THEN 100.0 * COUNT(CASE WHEN __value = 0 OR __value IS NULL THEN 1 END) / COUNT(*)
                    ELSE 0.0
                END AS zero_pct
            FROM base_data
            GROUP BY __uid
        ),
        series_agg AS (
            SELECT 
                __uid,
                LIST(__value ORDER BY __date) AS values
            FROM base_data
            WHERE __value IS NOT NULL
            GROUP BY __uid
            HAVING COUNT(*) >= 7
        ),
        seasonality_results AS (
            SELECT 
                __uid,
                values,
                TS_DETECT_SEASONALITY(values) AS detected_periods
            FROM series_agg
        ),
        ordered_data AS (
            SELECT 
                __uid,
                __date,
                __value,
                ROW_NUMBER() OVER (PARTITION BY __uid ORDER BY __date) AS row_num
            FROM base_data
            WHERE __value IS NOT NULL
        ),
        trend_stats AS (
            SELECT 
                __uid,
                COUNT(*) AS n_points,
                CORR(row_num, __value) AS trend_correlation
            FROM ordered_data
            GROUP BY __uid
            HAVING COUNT(*) >= 3
        )"
     R"(
        ),
        -- Dimension 1: Structural Integrity
        structural_checks AS (
            SELECT 
                __uid AS unique_id,
                'Structural' AS dimension,
                'key_uniqueness' AS metric,
                CASE 
                    WHEN n_duplicates > 0 
                    THEN n_duplicates || ' duplicate pairs found'
                    ELSE 'No duplicates'
                END AS value
            FROM duplicate_stats
            
            UNION ALL
            
            SELECT 
                'ALL_SERIES' AS unique_id,
                'Structural' AS dimension,
                'id_cardinality' AS metric,
                COUNT(DISTINCT __uid) || ' unique IDs' AS value
            FROM base_data
        ),
        -- Dimension 2: Temporal Integrity
        temporal_checks AS (
            SELECT 
                ss.__uid AS unique_id,
                'Temporal' AS dimension,
                'series_length' AS metric,
                ss.length || ' observations' AS value
            FROM series_stats ss
            CROSS JOIN params
            
            UNION ALL
            
            SELECT 
                __uid AS unique_id,
                'Temporal' AS dimension,
                'timestamp_gaps' AS metric,
                ROUND(gap_pct, 1) || '% gaps (' || n_gaps || ' missing dates)' AS value
            FROM gap_stats
            
            UNION ALL
            
            SELECT 
                'ALL_SERIES' AS unique_id,
                'Temporal' AS dimension,
                'series_alignment' AS metric,
                CASE 
                    WHEN n_start_dates > 1 OR n_end_dates > 1 
                    THEN 'Ragged edges: ' || n_start_dates || ' start dates, ' || n_end_dates || ' end dates'
                    ELSE 'All series aligned'
                END AS value
            FROM alignment_stats
            
            UNION ALL
            
            SELECT 
                'ALL_SERIES' AS unique_id,
                'Temporal' AS dimension,
                'frequency_inference' AS metric,
                CASE 
                    WHEN n_frequencies > 1 
                    THEN 'Mixed frequencies detected across ' || n_series || ' series'
                    ELSE 'Consistent frequency across all series'
                END AS value
            FROM frequency_diversity
        ),
        -- Dimension 3: Magnitude & Value Validity
        magnitude_checks AS (
            SELECT 
                __uid AS unique_id,
                'Magnitude' AS dimension,
                'missing_values' AS metric,
                ROUND(null_pct, 1) || '% missing (' || null_count || ' NULLs)' AS value
            FROM missing_stats
            
            UNION ALL
            
            SELECT 
                __uid AS unique_id,
                'Magnitude' AS dimension,
                'value_bounds' AS metric,
                CASE 
                    WHEN negative_count > 0 
                    THEN negative_count || ' negative values found'
                    ELSE 'No negative values'
                END AS value
            FROM negative_stats
            
            UNION ALL
            
            SELECT 
                __uid AS unique_id,
                'Magnitude' AS dimension,
                'static_values' AS metric,
                CASE 
                    WHEN distinct_count = 1 OR (stddev IS NOT NULL AND stddev = 0)
                    THEN 'Constant series (variance = 0)'
                    ELSE 'Variable series'
                END AS value
            FROM variance_stats
        ),
        -- Dimension 4: Behavioural/Statistical (Advanced)
        behavioural_checks AS (
            SELECT 
                __uid AS unique_id,
                'Behavioural' AS dimension,
                'intermittency' AS metric,
                ROUND(zero_pct, 1) || '% zeros' AS value
            FROM zero_stats
            
            UNION ALL
            
            SELECT 
                __uid AS unique_id,
                'Behavioural' AS dimension,
                'seasonality_check' AS metric,
                CASE 
                    WHEN LEN(detected_periods) = 0 
                    THEN 'No seasonality detected'
                    ELSE 'Seasonality detected: periods ' || detected_periods::VARCHAR
                END AS value
            FROM seasonality_results
            
            UNION ALL
            
            SELECT 
                __uid AS unique_id,
                'Behavioural' AS dimension,
                'trend_detection' AS metric,
                CASE 
                    WHEN ABS(trend_correlation) > 0.7 
                    THEN CASE 
                        WHEN trend_correlation > 0 THEN 'Strong positive trend (r=' || ROUND(trend_correlation, 2) || ')'
                        ELSE 'Strong negative trend (r=' || ROUND(trend_correlation, 2) || ')'
                    END
                    ELSE 'No strong trend detected (r=' || ROUND(COALESCE(trend_correlation, 0), 2) || ')'
                END AS value
            FROM trend_stats
        ),
        -- Combine all checks
        all_checks AS (
            SELECT * FROM structural_checks
            UNION ALL
            SELECT * FROM temporal_checks
            UNION ALL
            SELECT * FROM magnitude_checks
            UNION ALL
            SELECT * FROM behavioural_checks
        )
        SELECT 
            unique_id,
            dimension,
            metric,
            value
        FROM all_checks
        ORDER BY 
            dimension,
            metric,
            unique_id
    )"},

    // TS_DATA_QUALITY_SUMMARY: Aggregated summary by dimension
    {DEFAULT_SCHEMA,
     "ts_data_quality_summary",
     {"table_name", "unique_id_col", "date_col", "value_col", "n_short", nullptr},
     {{nullptr, nullptr}},
     R"(
        WITH health_card AS (
            SELECT * FROM ts_data_quality_health_card(table_name, unique_id_col, date_col, value_col, n_short)
        )
        SELECT 
            dimension,
            metric,
            COUNT(*) AS total_series,
            COUNT(DISTINCT unique_id) AS unique_series_count
        FROM health_card
        WHERE unique_id != 'ALL_SERIES'
        GROUP BY dimension, metric
        ORDER BY 
            dimension,
            metric
    )"},

    // End marker
    {nullptr, nullptr, {nullptr}, {{nullptr, nullptr}}, nullptr}};

// Register Data Quality table macros
void RegisterDataQualityMacros(ExtensionLoader &loader) {
	for (idx_t index = 0; data_quality_macros[index].name != nullptr; index++) {
		auto table_info = DefaultTableFunctionGenerator::CreateTableMacroInfo(data_quality_macros[index]);
		table_info->on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
		loader.RegisterFunction(*table_info);
	}
}

} // namespace duckdb
