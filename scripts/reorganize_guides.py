#!/usr/bin/env python3
"""
Reorganize guides to follow logical workflow structure.
- Consistent naming: NN_descriptive_name.md
- Logical flow: analyze → prepare → forecast → evaluate
- Simple to complex progression
"""

import os
import shutil
from pathlib import Path

# Configuration
PROJECT_ROOT = Path(__file__).parent.parent
TEMPLATES_DIR = PROJECT_ROOT / "guides" / "templates"
SQL_DIR = PROJECT_ROOT / "test" / "sql" / "docs_examples"

# Mapping: old_name → new_name
GUIDE_MAPPING = {
    # Getting Started (00-09)
    "README.md.in": "00_README.md.in",
    "01_quickstart.md.in": "01_quickstart.md.in",  # Keep as-is

    # Understanding Data (10-19)
    "40_eda_data_prep.md.in": "11_exploratory_analysis.md.in",
    "SEASONALITY_API.md.in": "12_detecting_seasonality.md.in",
    "CHANGEPOINT_API.md.in": "13_detecting_changepoints.md.in",

    # Data Preparation (20-29)
    "EDA_DATA_PREP.md.in": "20_data_preparation.md.in",

    # Forecasting Basics (30-39)
    "03_basic_forecasting.md.in": "30_basic_forecasting.md.in",
    "20_understanding_forecasts.md.in": "31_understanding_forecasts.md.in",

    # Model Selection (40-49)
    "11_model_selection.md.in": "40_model_selection.md.in",
    "PARAMETERS.md.in": "41_model_parameters.md.in",
    "INSAMPLE_FORECAST.md.in": "42_insample_validation.md.in",

    # Evaluation (50-59)
    "METRICS.md.in": "50_evaluation_metrics.md.in",
    "USAGE.md.in": "51_usage_guide.md.in",

    # Optimization (60-69)
    "13_performance.md.in": "60_performance_optimization.md.in",

    # Use Cases (70-79)
    "30_demand_forecasting.md.in": "70_demand_forecasting.md.in",
    "31_sales_prediction.md.in": "71_sales_prediction.md.in",
    "32_capacity_planning.md.in": "72_capacity_planning.md.in",

    # Multi-Language (80-89)
    "49_multi_language_overview.md.in": "80_multi_language_overview.md.in",
    "50_python_usage.md.in": "81_python_integration.md.in",
    "51_r_usage.md.in": "82_r_integration.md.in",
    "52_julia_usage.md.in": "83_julia_integration.md.in",
    "53_cpp_usage.md.in": "84_cpp_integration.md.in",
    "54_rust_usage.md.in": "85_rust_integration.md.in",

    # Reference (90-99)
    "10_api_reference.md.in": "90_api_reference.md.in",
    "00_guide_index.md.in": "99_guide_index.md.in",

    # Meta (lowercase, no numbers)
    "DOCUMENTATION_GUIDE.md.in": "documentation_guide.md.in",
    "TESTING_GUIDE.md.in": "testing_guide.md.in",
    "TESTING_PLAN.md.in": "testing_plan.md.in",
    "FINAL_TEST_SUMMARY.md.in": "archived_final_test_summary.md.in",
}

# SQL file prefix mapping
SQL_PREFIX_MAPPING = {
    "40_eda_data_prep": "11_exploratory_analysis",
    "SEASONALITY_API": "12_detecting_seasonality",
    "CHANGEPOINT_API": "13_detecting_changepoints",
    "EDA_DATA_PREP": "20_data_preparation",
    "03_basic_forecasting": "30_basic_forecasting",
    "20_understanding_forecasts": "31_understanding_forecasts",
    "11_model_selection": "40_model_selection",
    "PARAMETERS": "41_model_parameters",
    "INSAMPLE_FORECAST": "42_insample_validation",
    "METRICS": "50_evaluation_metrics",
    "USAGE": "51_usage_guide",
    "13_performance": "60_performance_optimization",
    "30_demand_forecasting": "70_demand_forecasting",
    "31_sales_prediction": "71_sales_prediction",
    "32_capacity_planning": "72_capacity_planning",
    "49_multi_language_overview": "80_multi_language_overview",
    "50_python_usage": "81_python_integration",
    "51_r_usage": "82_r_integration",
    "52_julia_usage": "83_julia_integration",
    "53_cpp_usage": "84_cpp_integration",
    "54_rust_usage": "85_rust_integration",
    "10_api_reference": "90_api_reference",
    "00_guide_index": "99_guide_index",
    "DOCUMENTATION_GUIDE": "documentation_guide",
    "TESTING_GUIDE": "testing_guide",
    "TESTING_PLAN": "testing_plan",
    "FINAL_TEST_SUMMARY": "archived_final_test_summary",
}


def rename_template_files():
    """Rename template files according to mapping."""
    print("\n" + "=" * 70)
    print("STEP 1: Renaming Template Files")
    print("=" * 70)

    renamed = 0
    for old_name, new_name in GUIDE_MAPPING.items():
        old_path = TEMPLATES_DIR / old_name
        new_path = TEMPLATES_DIR / new_name

        if old_path.exists():
            if old_name != new_name:
                print(f"  {old_name:40s} → {new_name}")
                shutil.move(str(old_path), str(new_path))
                renamed += 1
            else:
                print(f"  {old_name:40s} (unchanged)")
        else:
            print(f"  {old_name:40s} (not found)")

    print(f"\n✅ Renamed {renamed} template files")
    return renamed


def rename_sql_files():
    """Rename SQL example files to match new guide names."""
    print("\n" + "=" * 70)
    print("STEP 2: Renaming SQL Example Files")
    print("=" * 70)

    renamed = 0
    sql_files = list(SQL_DIR.glob("*.sql"))

    for sql_file in sql_files:
        old_name = sql_file.name
        new_name = old_name

        # Check if filename starts with any old prefix
        for old_prefix, new_prefix in SQL_PREFIX_MAPPING.items():
            if old_name.startswith(old_prefix + "_"):
                new_name = old_name.replace(old_prefix + "_", new_prefix + "_", 1)
                break

        if new_name != old_name:
            new_path = SQL_DIR / new_name
            print(f"  {old_name:50s} → {new_name}")
            shutil.move(str(sql_file), str(new_path))
            renamed += 1

    print(f"\n✅ Renamed {renamed} SQL files")
    return renamed


def update_include_directives():
    """Update include directives in template files to match new SQL names."""
    print("\n" + "=" * 70)
    print("STEP 3: Updating Include Directives")
    print("=" * 70)

    updated_files = 0
    total_replacements = 0

    for template_file in TEMPLATES_DIR.glob("*.md.in"):
        content = template_file.read_text(encoding='utf-8')
        original_content = content
        replacements = 0

        # Replace SQL file references
        for old_prefix, new_prefix in SQL_PREFIX_MAPPING.items():
            old_pattern = f"test/sql/docs_examples/{old_prefix}_"
            new_pattern = f"test/sql/docs_examples/{new_prefix}_"

            if old_pattern in content:
                count = content.count(old_pattern)
                content = content.replace(old_pattern, new_pattern)
                replacements += count

        if content != original_content:
            template_file.write_text(content, encoding='utf-8')
            print(f"  {template_file.name:40s} ({replacements} replacements)")
            updated_files += 1
            total_replacements += replacements

    print(f"\n✅ Updated {updated_files} template files ({total_replacements} replacements)")
    return updated_files


def update_cross_references():
    """Update cross-references between guides (links to other guides)."""
    print("\n" + "=" * 70)
    print("STEP 4: Updating Cross-References")
    print("=" * 70)

    # Create reverse mapping for markdown references (without .in)
    md_mapping = {
        old.replace('.md.in', '.md'): new.replace('.md.in', '.md')
        for old, new in GUIDE_MAPPING.items()
    }

    updated_files = 0
    total_replacements = 0

    for template_file in TEMPLATES_DIR.glob("*.md.in"):
        content = template_file.read_text(encoding='utf-8')
        original_content = content
        replacements = 0

        # Replace markdown links
        for old_md, new_md in md_mapping.items():
            # Match patterns like: [text](old_name.md) or (old_name.md)
            if old_md != new_md and old_md in content:
                count = content.count(old_md)
                content = content.replace(old_md, new_md)
                replacements += count

        if content != original_content:
            template_file.write_text(content, encoding='utf-8')
            print(f"  {template_file.name:40s} ({replacements} link updates)")
            updated_files += 1
            total_replacements += replacements

    print(f"\n✅ Updated {updated_files} template files ({total_replacements} link updates)")
    return updated_files


def main():
    """Run all reorganization steps."""
    print("=" * 70)
    print("GUIDE REORGANIZATION")
    print("=" * 70)
    print("\nThis script will:")
    print("  1. Rename template files to follow NN_name.md.in convention")
    print("  2. Rename SQL example files to match new guide names")
    print("  3. Update include directives in templates")
    print("  4. Update cross-references between guides")
    print("\nAfter completion, run: make docs")
    print("=" * 70)

    input("\nPress Enter to continue or Ctrl+C to cancel...")

    # Execute reorganization steps
    template_count = rename_template_files()
    sql_count = rename_sql_files()
    include_count = update_include_directives()
    xref_count = update_cross_references()

    # Summary
    print("\n" + "=" * 70)
    print("REORGANIZATION COMPLETE")
    print("=" * 70)
    print(f"  Template files renamed:    {template_count}")
    print(f"  SQL files renamed:         {sql_count}")
    print(f"  Templates updated:         {include_count + xref_count}")
    print("=" * 70)
    print("\nNext steps:")
    print("  1. Run: make docs")
    print("  2. Review generated files in guides/")
    print("  3. Update 99_guide_index.md.in with new structure")
    print("  4. Update 00_README.md.in with new paths")
    print("  5. Commit changes")
    print("=" * 70)


if __name__ == "__main__":
    main()
