#!/bin/bash
# Contract Validation Script
# Run this before committing any contract changes
#
# Usage: ./validate.sh [--ci]
#   --ci: Exit with non-zero on warnings (for CI enforcement)

set -e

CONTRACTS_DIR="$(dirname "$0")/.."
cd "$CONTRACTS_DIR"

CI_MODE=false
if [[ "$1" == "--ci" ]]; then
    CI_MODE=true
fi

ERRORS=0
WARNINGS=0

echo "=== RS-1 Contract Validation ==="
echo ""

# Check for required tools
check_tool() {
    if ! command -v "$1" &> /dev/null; then
        echo "ERROR: Required tool '$1' not found. Install with: $2"
        exit 1
    fi
}

# Try npx ajv first, fall back to global ajv
AJV_CMD=""
if command -v npx &> /dev/null && npx ajv --version &> /dev/null 2>&1; then
    AJV_CMD="npx ajv"
elif command -v ajv &> /dev/null; then
    AJV_CMD="ajv"
else
    echo "ERROR: ajv-cli not found. Install with: npm install -g ajv-cli ajv-formats"
    exit 1
fi

check_tool "jq" "apt install jq / brew install jq"
check_tool "python3" "apt install python3"

echo "=== Layer 1: Schema Compilation ==="
for schema in schemas/*.schema.json; do
    if [[ ! -f "$schema" ]]; then
        continue
    fi
    echo "  Compiling: $(basename "$schema")"
    if ! $AJV_CMD compile --spec=draft2020 -s "$schema" 2>/dev/null; then
        echo "    ERROR: Schema compilation failed"
        ((ERRORS++))
    fi
done
echo ""

echo "=== Layer 2: Example Validation ==="
for schema in schemas/*.schema.json; do
    if [[ ! -f "$schema" ]]; then
        continue
    fi
    name=$(basename "$schema" .schema.json)
    example_dir="examples/$name"

    if [[ ! -d "$example_dir" ]]; then
        echo "  WARNING: No examples directory for $name"
        ((WARNINGS++))
        continue
    fi

    echo "  Validating examples for: $name"

    # Valid examples should pass
    for valid in "$example_dir"/valid-*.json; do
        if [[ ! -f "$valid" ]]; then
            continue
        fi
        if $AJV_CMD validate -s "$schema" -d "$valid" --spec=draft2020 2>/dev/null; then
            echo "    PASS: $(basename "$valid")"
        else
            echo "    FAIL: $(basename "$valid") should be valid"
            ((ERRORS++))
        fi
    done

    # Invalid examples should fail
    for invalid in "$example_dir"/invalid-*.json; do
        if [[ ! -f "$invalid" ]]; then
            continue
        fi
        if $AJV_CMD validate -s "$schema" -d "$invalid" --spec=draft2020 2>/dev/null; then
            echo "    FAIL: $(basename "$invalid") should be invalid but passed"
            ((ERRORS++))
        else
            echo "    PASS: $(basename "$invalid") (correctly rejected)"
        fi
    done
done
echo ""

echo "=== Layer 3: Golden File Regression ==="
golden_count=0
for golden in golden/**/*.json; do
    if [[ ! -f "$golden" ]]; then
        continue
    fi
    ((golden_count++))

    # Infer schema from path
    if [[ "$golden" == *"telemetry"* ]]; then
        schema="schemas/telemetry.schema.json"
    elif [[ "$golden" == *"device-state"* ]]; then
        schema="schemas/device-state.schema.json"
    elif [[ "$golden" == *"ota"* ]]; then
        schema="schemas/ota-manifest.schema.json"
    elif [[ "$golden" == *"zone"* ]]; then
        schema="schemas/zone-config.schema.json"
    else
        echo "  WARNING: Cannot infer schema for $golden"
        ((WARNINGS++))
        continue
    fi

    echo "  Checking: $(basename "$golden") against $(basename "$schema")"
    if ! $AJV_CMD validate -s "$schema" -d "$golden" --spec=draft2020 2>/dev/null; then
        echo "    FAIL: Golden file no longer valid"
        ((ERRORS++))
    fi
done

if [[ $golden_count -eq 0 ]]; then
    echo "  INFO: No golden files found (this is OK for initial setup)"
fi
echo ""

echo "=== Layer 4: Breaking Change Detection ==="
if [[ -f "tooling/check-breaking.py" ]]; then
    if git rev-parse --verify HEAD~1 >/dev/null 2>&1; then
        python3 tooling/check-breaking.py || ((WARNINGS++))
    else
        echo "  INFO: No git history available for breaking change detection"
    fi
else
    echo "  INFO: check-breaking.py not found, skipping"
fi
echo ""

echo "=== Summary ==="
echo "  Errors:   $ERRORS"
echo "  Warnings: $WARNINGS"
echo ""

if [[ $ERRORS -gt 0 ]]; then
    echo "FAILED: Contract validation found errors."
    exit 1
fi

if [[ $CI_MODE == true && $WARNINGS -gt 0 ]]; then
    echo "FAILED: Contract validation found warnings (CI mode)."
    exit 1
fi

echo "PASSED: All contract validations successful."
exit 0
