#!/usr/bin/env python3
"""
Transform markdown documentation by extracting SQL examples into separate files.
Converts .md files to .md.in templates with include directives.
"""

import os
import re
import sys
from pathlib import Path
from typing import List, Tuple

# Configuration
PROJECT_ROOT = Path(__file__).parent.parent
GUIDES_DIR = PROJECT_ROOT / "guides"
DOCS_DIR = PROJECT_ROOT / "docs"
TEMPLATES_DIR = PROJECT_ROOT / "guides" / "templates"
SQL_DIR = PROJECT_ROOT / "test" / "sql" / "docs_examples"


def extract_sql_blocks(content: str) -> List[Tuple[int, str]]:
    """
    Extract SQL code blocks from markdown content.
    Returns list of (line_number, sql_content) tuples.
    """
    blocks = []
    in_sql_block = False
    current_block = []
    block_start_line = 0

    for i, line in enumerate(content.split('\n'), 1):
        if line.strip().startswith('```sql'):
            in_sql_block = True
            block_start_line = i
            current_block = []
        elif line.strip() == '```' and in_sql_block:
            in_sql_block = False
            if current_block:
                blocks.append((block_start_line, '\n'.join(current_block)))
        elif in_sql_block:
            current_block.append(line)

    return blocks


def create_sql_filename(context: str, index: int, guide_name: str) -> str:
    """
    Create a descriptive filename for SQL examples.
    Uses context from surrounding markdown to create meaningful names.
    """
    # Extract guide base name (e.g., "01_quickstart" from "01_quickstart.md")
    guide_base = guide_name.replace('.md', '').replace('.md.in', '')

    # Try to extract a meaningful name from context
    # Look for headers (##, ###) or key phrases
    context_lower = context.lower()

    # Common patterns in the content
    if 'load extension' in context_lower or 'load the extension' in context_lower:
        return f"{guide_base}_load_extension.sql"
    elif 'create table' in context_lower or 'sample data' in context_lower:
        return f"{guide_base}_create_sample_data_{index:02d}.sql"
    elif 'forecast' in context_lower and 'generate' in context_lower:
        return f"{guide_base}_forecast_{index:02d}.sql"
    elif 'multiple series' in context_lower or 'forecast_by' in context_lower:
        return f"{guide_base}_multi_series_{index:02d}.sql"
    elif 'visualize' in context_lower or 'ascii' in context_lower:
        return f"{guide_base}_visualization_{index:02d}.sql"
    elif 'evaluate' in context_lower or 'accuracy' in context_lower or 'metrics' in context_lower:
        return f"{guide_base}_evaluate_{index:02d}.sql"
    elif 'quality' in context_lower or 'check data' in context_lower:
        return f"{guide_base}_data_quality_{index:02d}.sql"
    elif 'stats' in context_lower or 'statistics' in context_lower:
        return f"{guide_base}_statistics_{index:02d}.sql"
    elif 'fill gaps' in context_lower or 'fill_gaps' in context_lower:
        return f"{guide_base}_fill_gaps_{index:02d}.sql"
    elif 'seasonality' in context_lower or 'detect' in context_lower:
        return f"{guide_base}_seasonality_{index:02d}.sql"
    elif 'compare models' in context_lower:
        return f"{guide_base}_model_comparison_{index:02d}.sql"
    elif 'complete example' in context_lower or 'workflow' in context_lower:
        return f"{guide_base}_complete_example_{index:02d}.sql"
    else:
        return f"{guide_base}_example_{index:02d}.sql"


def get_context(content: str, block_start: int, lines_before: int = 10) -> str:
    """Get context lines before a SQL block to infer its purpose."""
    lines = content.split('\n')
    start = max(0, block_start - lines_before - 1)
    end = block_start - 1
    return '\n'.join(lines[start:end])


def transform_markdown_file(md_path: Path, is_guide: bool = True) -> None:
    """
    Transform a markdown file:
    1. Extract SQL blocks to separate files
    2. Create .md.in template with include directives
    """
    print(f"\n📄 Processing: {md_path.name}")

    # Read original content
    with open(md_path, 'r', encoding='utf-8') as f:
        content = f.read()

    # Extract SQL blocks
    sql_blocks = extract_sql_blocks(content)

    if not sql_blocks:
        print(f"   ℹ️  No SQL blocks found")
        # Still create template for consistency
        template_path = TEMPLATES_DIR / md_path.name.replace('.md', '.md.in')
        with open(template_path, 'w', encoding='utf-8') as f:
            f.write(content)
        return

    print(f"   Found {len(sql_blocks)} SQL block(s)")

    # Create SQL files and track mappings
    sql_file_mappings = {}

    for idx, (line_num, sql_content) in enumerate(sql_blocks, 1):
        # Get context to create meaningful filename
        context = get_context(content, line_num)
        sql_filename = create_sql_filename(context, idx, md_path.name)
        sql_path = SQL_DIR / sql_filename

        # Create SQL file
        SQL_DIR.mkdir(parents=True, exist_ok=True)
        with open(sql_path, 'w', encoding='utf-8') as f:
            f.write(sql_content.rstrip() + '\n')

        print(f"   ✅ Created: test/sql/docs_examples/{sql_filename}")

        # Track mapping for template replacement
        sql_file_mappings[line_num] = f"test/sql/docs_examples/{sql_filename}"

    # Create template file with include directives
    template_content = []
    lines = content.split('\n')
    i = 0

    while i < len(lines):
        line = lines[i]

        # Check if this is the start of a SQL block
        if line.strip().startswith('```sql'):
            # Find which SQL block this is (by approximate line number)
            block_line = i + 1
            matching_block = None
            for sql_line_num, sql_file in sql_file_mappings.items():
                # Allow some tolerance for line number matching
                if abs(sql_line_num - block_line) < 3:
                    matching_block = sql_file
                    break

            if matching_block:
                # Replace SQL block with include directive
                template_content.append(f"<!-- include: {matching_block} -->")

                # Skip until end of code block
                i += 1
                while i < len(lines) and lines[i].strip() != '```':
                    i += 1
                i += 1  # Skip the closing ```
                continue

        template_content.append(line)
        i += 1

    # Write template file
    template_path = TEMPLATES_DIR / md_path.name.replace('.md', '.md.in')
    with open(template_path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(template_content))

    print(f"   ✅ Created template: guides/templates/{template_path.name}")


def main():
    """Main transformation process."""
    print("=" * 70)
    print("📚 Documentation Transformation")
    print("=" * 70)

    # Create directories
    TEMPLATES_DIR.mkdir(parents=True, exist_ok=True)
    SQL_DIR.mkdir(parents=True, exist_ok=True)

    # Process all markdown files in guides/
    guide_files = sorted(GUIDES_DIR.glob("*.md"))

    print(f"\n🔍 Found {len(guide_files)} guide files")

    for md_file in guide_files:
        if md_file.name != 'README.md':  # Skip README for now, handle separately
            transform_markdown_file(md_file, is_guide=True)

    # Process all markdown files in docs/ (if exists)
    if DOCS_DIR.exists():
        doc_files = sorted(DOCS_DIR.glob("*.md"))
        print(f"\n🔍 Found {len(doc_files)} documentation files")

        for md_file in doc_files:
            transform_markdown_file(md_file, is_guide=False)

    print("\n" + "=" * 70)
    print("✅ Transformation complete!")
    print("=" * 70)
    print("\nNext steps:")
    print("1. Review extracted SQL files in test/sql/docs_examples/")
    print("2. Review template files in guides/templates/")
    print("3. Run 'make docs' to build final documentation")
    print("=" * 70)


if __name__ == "__main__":
    main()
