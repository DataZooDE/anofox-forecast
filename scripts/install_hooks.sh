#!/bin/bash

set -euo pipefail

HOOK_DIR=".git/hooks"

echo "📦 Installing git hooks..."

# Pre-commit Hook
cat > "$HOOK_DIR/pre-commit" <<'EOF'
#!/bin/bash

echo "🔨 Building documentation..."
bash scripts/build_docs.sh

if [ $? -ne 0 ]; then
    echo "❌ Documentation build failed!"
    exit 1
fi

echo "📝 Linting markdown files..."
if command -v markdownlint &> /dev/null; then
    if ! markdownlint 'guides/**/*.md' --ignore node_modules 2>/dev/null; then
        echo "❌ Markdownlint found issues!"
        echo "Run 'markdownlint --fix guides/**/*.md' to auto-fix some issues"
        exit 1
    fi
    echo "✅ Markdown files passed linting"
else
    echo "⚠️  Warning: markdownlint not found, skipping markdown linting"
    echo "   Install with: npm install -g markdownlint-cli"
fi

echo "🧪 Testing SQL examples..."
bash scripts/test_sql_examples.sh

if [ $? -ne 0 ]; then
    echo "❌ SQL example tests failed!"
    echo "Fix the examples or use 'git commit --no-verify' to skip"
    exit 1
fi

# Stage the generated markdown files for commit
# This ensures GitHub visitors see the complete documentation
if [ -d "guides" ]; then
    find guides -name "*.md" -not -name "*.md.in" -type f -exec git add {} \;
fi

echo "✅ Documentation built, linted, tested, and staged for commit"
EOF

chmod +x "$HOOK_DIR/pre-commit"

echo "✅ Git hooks installed successfully"
echo ""
echo "The pre-commit hook will now:"
echo "  1. Build documentation from templates in guides/templates/"
echo "  2. Lint markdown files in guides/ with markdownlint"
echo "  3. Test all SQL examples"
echo "  4. Stage generated .md files in guides/ for commit (visible on GitHub)"
echo ""
echo "To skip hooks during commit, use: git commit --no-verify"
