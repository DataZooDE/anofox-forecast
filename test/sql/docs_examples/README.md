# Documentation SQL Examples

This directory contains all SQL code examples extracted from the documentation. Each example is:
- ✅ **Testable**: Can be run independently
- ✅ **Referenced**: Embedded in documentation via templates
- ✅ **Maintained**: Single source of truth for SQL code

## Directory Structure

```
docs_examples/
├── 01_quickstart_*.sql          - Quick start guide examples
├── 03_basic_forecasting_*.sql   - Basic forecasting workflow
├── 10_api_reference_*.sql       - API reference examples
├── 11_model_selection_*.sql     - Model selection examples
├── 13_performance_*.sql         - Performance optimization
├── 20_understanding_*.sql       - Statistical concepts
├── 30_demand_forecasting_*.sql  - Demand forecasting use case
├── 31_sales_prediction_*.sql    - Sales prediction use case
├── 32_capacity_planning_*.sql   - Capacity planning use case
├── 40_eda_data_prep_*.sql       - EDA and data preparation
├── 49_multi_language_*.sql      - Multi-language examples
└── *_*.sql                      - Various documentation examples
```

## Example Categories

### By Type

- **`*_load_extension.sql`** - Extension loading examples
- **`*_create_sample_data_*.sql`** - Sample data creation
- **`*_forecast_*.sql`** - Forecasting examples
- **`*_multi_series_*.sql`** - Multiple series forecasting
- **`*_evaluate_*.sql`** - Accuracy evaluation
- **`*_data_quality_*.sql`** - Data quality checks
- **`*_statistics_*.sql`** - Statistical analysis
- **`*_seasonality_*.sql`** - Seasonality detection
- **`*_fill_gaps_*.sql`** - Gap filling operations
- **`*_visualization_*.sql`** - Data visualization
- **`*_model_comparison_*.sql`** - Model comparison
- **`*_complete_example_*.sql`** - Complete workflows

### By Guide

Total examples per guide:

| Guide | SQL Files | Description |
|-------|-----------|-------------|
| 01_quickstart | 11 | Quick start guide |
| 03_basic_forecasting | 18 | Basic forecasting workflow |
| 10_api_reference | 46 | API reference examples |
| 11_model_selection | 22 | Model selection guide |
| 13_performance | 37 | Performance optimization |
| 20_understanding_forecasts | 28 | Statistical concepts |
| 30_demand_forecasting | 19 | Demand forecasting |
| 31_sales_prediction | 20 | Sales prediction |
| 32_capacity_planning | 17 | Capacity planning |
| 40_eda_data_prep | 24 | EDA and data prep |
| 49_multi_language | 2 | Multi-language usage |
| CHANGEPOINT_API | 16 | Changepoint detection |
| EDA_DATA_PREP | 34 | EDA macros documentation |
| INSAMPLE_FORECAST | 16 | In-sample forecasting |
| METRICS | 40 | Evaluation metrics |
| PARAMETERS | 50 | Parameter documentation |
| Other docs | 53 | Various documentation |

**Total: 453 SQL examples**

## Usage

### Testing Examples

```bash
# Test all examples
make test-docs

# Test a specific example
duckdb :memory: < test/sql/docs_examples/01_quickstart_forecast_03.sql
```

### Using in Documentation

In template files (`guides/templates/*.md.in`):

```markdown
<!-- include: test/sql/docs_examples/01_quickstart_forecast_03.sql -->
```

This will be replaced with:

````markdown
```sql
[SQL code content]
```
````

### Adding New Examples

1. Create a new `.sql` file in this directory
2. Add the include directive in the appropriate template file
3. Build documentation: `make docs`
4. Test the example: `make test-docs`

## Example Naming Convention

Format: `{guide_name}_{category}_{number}.sql`

- **guide_name**: Source guide (e.g., `01_quickstart`, `10_api_reference`)
- **category**: Type of example (e.g., `forecast`, `evaluate`, `data_quality`)
- **number**: Sequential number (e.g., `01`, `02`, `03`)

Examples:
- `01_quickstart_forecast_03.sql` - Forecasting example from quickstart guide
- `10_api_reference_evaluate_08.sql` - Evaluation example from API reference
- `40_eda_data_prep_statistics_05.sql` - Statistics example from EDA guide

## Quality Standards

Each SQL example should:

1. **Be self-contained**: Include necessary setup
2. **Be runnable**: Work when executed directly
3. **Have comments**: Explain what it demonstrates
4. **Follow style**: Consistent SQL formatting
5. **Be minimal**: Show only what's needed

## Maintenance

- **Location**: All examples are in `test/sql/docs_examples/`
- **Templates**: Documentation templates reference these files
- **Generation**: Build process embeds them in final docs
- **Testing**: CI/CD can validate all examples work

## CI/CD Integration

The pre-commit hook automatically:
1. Builds documentation from templates
2. Tests all SQL examples
3. Lints generated markdown files
4. Stages generated files for commit

To install: `make install-hooks`

## Related Files

- **Templates**: `guides/templates/*.md.in` - Documentation templates
- **Generated**: `guides/*.md` - Built documentation (committed)
- **Build script**: `scripts/build_docs.sh` - Builds documentation
- **Test script**: `scripts/test_sql_examples.sh` - Tests examples
- **Makefile**: Top-level build targets
