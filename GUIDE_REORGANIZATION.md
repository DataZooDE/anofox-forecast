# Guide Reorganization Plan

## Current Issues

1. **Inconsistent Naming**:
   - Mix of numbered (01_quickstart) and non-numbered (METRICS)
   - Mix of uppercase (PARAMETERS) and lowercase (quickstart)
   - No clear numbering scheme

2. **Illogical Organization**:
   - Doesn't follow learning progression (simple → complex)
   - Doesn't follow forecasting workflow (analyze → prepare → forecast → evaluate)
   - EDA guide is at position 40, should be early
   - API reference scattered across multiple files

## Proposed Structure

Following the natural forecasting workflow:

### Phase 0: Getting Started (00-09)
```
00_README.md                    - Overview and navigation
01_quickstart.md                - 5-minute quick start (skip prep)
02_installation.md              - Installation guide (if needed)
```

### Phase 1: Understanding Data (10-19)
```
10_understanding_time_series.md - What makes good time series data
11_exploratory_analysis.md      - How to explore your data (EDA)
12_detecting_seasonality.md     - Finding seasonal patterns
13_detecting_changepoints.md    - Finding regime changes
```

### Phase 2: Data Preparation (20-29)
```
20_data_quality_checks.md       - Identifying data issues
21_data_cleaning.md             - Fixing common problems
22_handling_gaps.md             - Filling missing timestamps
23_handling_nulls.md            - Imputation strategies
```

### Phase 3: Forecasting Basics (30-39)
```
30_basic_forecasting.md         - Your first forecasts
31_understanding_forecasts.md   - Interpreting results
32_confidence_intervals.md      - Understanding uncertainty
```

### Phase 4: Model Selection (40-49)
```
40_model_selection.md           - Choosing the right model
41_model_parameters.md          - Tuning model parameters
42_insample_validation.md       - Validating with in-sample forecasts
```

### Phase 5: Evaluation (50-59)
```
50_evaluation_metrics.md        - Measuring forecast accuracy
51_forecast_validation.md       - Testing forecast quality
```

### Phase 6: Optimization (60-69)
```
60_performance_optimization.md  - Making forecasts faster
61_scaling_strategies.md        - Handling many series
```

### Phase 7: Use Cases (70-79)
```
70_demand_forecasting.md        - Retail & inventory use case
71_sales_prediction.md          - Revenue forecasting use case
72_capacity_planning.md         - Resource allocation use case
```

### Phase 8: Multi-Language (80-89)
```
80_multi_language_overview.md   - Cross-language usage
81_python_integration.md        - Python usage
82_r_integration.md             - R usage
83_julia_integration.md         - Julia usage
84_cpp_integration.md           - C++ usage
85_rust_integration.md          - Rust usage
```

### Phase 9: Reference (90-99)
```
90_api_reference.md             - Complete API documentation
91_function_reference.md        - All functions listed
92_parameter_reference.md       - All parameters documented
99_guide_index.md               - Complete guide index
```

### Meta (lowercase, for maintainers)
```
documentation_guide.md          - How to maintain docs
testing_guide.md                - Testing documentation
testing_plan.md                 - Test planning
```

## Mapping: Old → New

### Getting Started
- `01_quickstart.md` → `01_quickstart.md` (keep)
- `README.md` → `00_README.md`

### Understanding Data
- `40_eda_data_prep.md` → `11_exploratory_analysis.md`
- `EDA_DATA_PREP.md` → merge into `11_exploratory_analysis.md`
- `SEASONALITY_API.md` → `12_detecting_seasonality.md`
- `CHANGEPOINT_API.md` → `13_detecting_changepoints.md`

### Data Preparation
- Extract from `40_eda_data_prep.md` → `20_data_quality_checks.md`
- Extract from `40_eda_data_prep.md` → `21_data_cleaning.md`
- Extract from `EDA_DATA_PREP.md` → `22_handling_gaps.md`
- Extract from `EDA_DATA_PREP.md` → `23_handling_nulls.md`

### Forecasting
- `03_basic_forecasting.md` → `30_basic_forecasting.md`
- `20_understanding_forecasts.md` → `31_understanding_forecasts.md`
- Add: `32_confidence_intervals.md` (extract from other guides)

### Model Selection
- `11_model_selection.md` → `40_model_selection.md`
- `PARAMETERS.md` → `41_model_parameters.md`
- `INSAMPLE_FORECAST.md` → `42_insample_validation.md`

### Evaluation
- `METRICS.md` → `50_evaluation_metrics.md`
- `USAGE.md` → merge into relevant guides

### Optimization
- `13_performance.md` → `60_performance_optimization.md`

### Use Cases
- `30_demand_forecasting.md` → `70_demand_forecasting.md`
- `31_sales_prediction.md` → `71_sales_prediction.md`
- `32_capacity_planning.md` → `72_capacity_planning.md`

### Multi-Language
- `49_multi_language_overview.md` → `80_multi_language_overview.md`
- `50_python_usage.md` → `81_python_integration.md`
- `51_r_usage.md` → `82_r_integration.md`
- `52_julia_usage.md` → `83_julia_integration.md`
- `53_cpp_usage.md` → `84_cpp_integration.md`
- `54_rust_usage.md` → `85_rust_integration.md`

### Reference
- `10_api_reference.md` → `90_api_reference.md`
- `PARAMETERS.md` → incorporate into `92_parameter_reference.md`
- `00_guide_index.md` → `99_guide_index.md`

### Meta
- `DOCUMENTATION_GUIDE.md` → `documentation_guide.md`
- `TESTING_GUIDE.md` → `testing_guide.md`
- `TESTING_PLAN.md` → `testing_plan.md`
- `FINAL_TEST_SUMMARY.md` → remove or archive

## Naming Convention

All guide files follow: `NN_descriptive_name.md`

- `NN` = two-digit number (00-99)
- `descriptive_name` = lowercase with underscores
- `.md` = markdown extension

**Exception**: Meta/development files use lowercase without numbers:
- `documentation_guide.md`
- `testing_guide.md`

## Learning Paths

After reorganization, users can follow clear paths:

**Path 1: Quick Start** (fastest)
```
01_quickstart.md → 30_basic_forecasting.md → 40_model_selection.md
```

**Path 2: Thorough** (recommended)
```
01_quickstart.md
↓
11_exploratory_analysis.md
↓
20_data_quality_checks.md → 21_data_cleaning.md
↓
30_basic_forecasting.md → 31_understanding_forecasts.md
↓
40_model_selection.md
↓
50_evaluation_metrics.md
```

**Path 3: Specific Use Case**
```
01_quickstart.md → 70_demand_forecasting.md (with references)
```

## Implementation Steps

1. Create reorganization script
2. Rename template files in `guides/templates/`
3. Rename SQL example files in `test/sql/docs_examples/`
4. Update include directives in templates
5. Rebuild documentation
6. Update cross-references between guides
7. Update 99_guide_index.md
8. Update 00_README.md
9. Commit changes

## Benefits

✅ **Logical Flow**: Follows actual forecasting workflow
✅ **Easy to Learn**: Clear progression from simple to complex
✅ **Consistent**: All files follow same naming pattern
✅ **Organized**: Numbered ranges show related content
✅ **Findable**: Easy to locate specific topics
✅ **Maintainable**: Clear structure for future additions
