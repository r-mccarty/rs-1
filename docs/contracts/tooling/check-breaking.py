#!/usr/bin/env python3
"""
Comprehensive breaking change detector for JSON Schema.

Detects breaking changes between schema versions including:
- New required fields
- Type changes and narrowing
- Constraint tightening (min/max length, patterns, etc.)
- Enum value removal
- additionalProperties changes
- Property removal

Usage:
    python3 check-breaking.py                    # Compare HEAD~1 to HEAD
    python3 check-breaking.py --base main        # Compare main to HEAD
    python3 check-breaking.py --output-format github  # Output as GitHub PR comment
"""

import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import Any


def get_schema_at_ref(schema_path: str, git_ref: str) -> dict | None:
    """Get schema content at a specific git ref."""
    try:
        result = subprocess.run(
            ["git", "show", f"{git_ref}:{schema_path}"],
            capture_output=True,
            text=True,
            check=True,
        )
        return json.loads(result.stdout)
    except (subprocess.CalledProcessError, json.JSONDecodeError):
        return None


def is_type_widening(old_type: Any, new_type: Any) -> bool:
    """Check if type change is widening (non-breaking)."""
    if old_type == new_type:
        return True

    # string -> [string, null] is widening
    if isinstance(new_type, list) and old_type in new_type:
        return True

    # integer -> number is widening
    if old_type == "integer" and new_type == "number":
        return True

    return False


def check_property_breaking(name: str, old_def: dict, new_def: dict) -> list[str]:
    """Check a single property for breaking changes."""
    issues = []

    # Type changes
    old_type = old_def.get("type")
    new_type = new_def.get("type")
    if old_type and new_type and old_type != new_type:
        if not is_type_widening(old_type, new_type):
            issues.append(f"BREAKING: '{name}' type changed from {old_type} to {new_type}")

    # String constraints
    old_max_len = old_def.get("maxLength", float("inf"))
    new_max_len = new_def.get("maxLength", float("inf"))
    if old_max_len > new_max_len:
        issues.append(f"BREAKING: '{name}' maxLength decreased from {old_max_len} to {new_max_len}")

    old_min_len = old_def.get("minLength", 0)
    new_min_len = new_def.get("minLength", 0)
    if old_min_len < new_min_len:
        issues.append(f"BREAKING: '{name}' minLength increased from {old_min_len} to {new_min_len}")

    # Numeric constraints
    old_max = old_def.get("maximum", float("inf"))
    new_max = new_def.get("maximum", float("inf"))
    if old_max > new_max:
        issues.append(f"BREAKING: '{name}' maximum decreased from {old_max} to {new_max}")

    old_min = old_def.get("minimum", float("-inf"))
    new_min = new_def.get("minimum", float("-inf"))
    if old_min < new_min:
        issues.append(f"BREAKING: '{name}' minimum increased from {old_min} to {new_min}")

    # Pattern changes
    old_pattern = old_def.get("pattern")
    new_pattern = new_def.get("pattern")
    if old_pattern and new_pattern and old_pattern != new_pattern:
        issues.append(f"BREAKING: '{name}' pattern changed from '{old_pattern}' to '{new_pattern}'")

    # Enum changes
    if "enum" in old_def and "enum" in new_def:
        old_values = set(old_def["enum"])
        new_values = set(new_def["enum"])
        removed = old_values - new_values
        if removed:
            issues.append(f"BREAKING: '{name}' enum values removed: {removed}")

    # Array constraints
    old_max_items = old_def.get("maxItems", float("inf"))
    new_max_items = new_def.get("maxItems", float("inf"))
    if old_max_items > new_max_items:
        issues.append(f"BREAKING: '{name}' maxItems decreased from {old_max_items} to {new_max_items}")

    old_min_items = old_def.get("minItems", 0)
    new_min_items = new_def.get("minItems", 0)
    if old_min_items < new_min_items:
        issues.append(f"BREAKING: '{name}' minItems increased from {old_min_items} to {new_min_items}")

    return issues


def check_breaking_changes(old_schema: dict, new_schema: dict) -> list[str]:
    """
    Returns list of breaking change descriptions, empty if compatible.
    """
    issues = []

    # Check required fields
    old_required = set(old_schema.get("required", []))
    new_required = set(new_schema.get("required", []))
    added_required = new_required - old_required
    if added_required:
        issues.append(f"BREAKING: New required fields: {added_required}")

    removed_required = old_required - new_required
    # Removing required is non-breaking (loosening)

    # Check each property
    old_props = old_schema.get("properties", {})
    new_props = new_schema.get("properties", {})

    for prop_name, old_def in old_props.items():
        if prop_name not in new_props:
            issues.append(f"BREAKING: Property '{prop_name}' removed")
            continue

        new_def = new_props[prop_name]
        prop_issues = check_property_breaking(prop_name, old_def, new_def)
        issues.extend(prop_issues)

    # Check additionalProperties
    old_additional = old_schema.get("additionalProperties", True)
    new_additional = new_schema.get("additionalProperties", True)
    if old_additional is True and new_additional is False:
        issues.append("BREAKING: additionalProperties changed from true to false")

    # Check enum at root level
    if "enum" in old_schema and "enum" in new_schema:
        old_values = set(old_schema["enum"])
        new_values = set(new_schema["enum"])
        removed = old_values - new_values
        if removed:
            issues.append(f"BREAKING: Root enum values removed: {removed}")

    # Check oneOf/anyOf for removed options
    for keyword in ["oneOf", "anyOf"]:
        if keyword in old_schema and keyword in new_schema:
            old_count = len(old_schema[keyword])
            new_count = len(new_schema[keyword])
            if new_count < old_count:
                issues.append(f"BREAKING: {keyword} options reduced from {old_count} to {new_count}")

    return issues


def main():
    parser = argparse.ArgumentParser(description="Detect breaking changes in JSON schemas")
    parser.add_argument("--base", default="HEAD~1", help="Base git ref to compare against")
    parser.add_argument("--head", default="HEAD", help="Head git ref to compare")
    parser.add_argument("--output-format", choices=["text", "github"], default="text",
                        help="Output format")
    args = parser.parse_args()

    # Find all schemas in current HEAD
    schema_dir = Path("schemas")
    if not schema_dir.exists():
        print("No schemas directory found, skipping breaking change detection")
        return 0

    all_issues = {}

    for schema_file in schema_dir.glob("*.schema.json"):
        schema_path = f"docs/contracts/{schema_file}"

        # Get old and new versions
        old_schema = get_schema_at_ref(schema_path, args.base)

        try:
            with open(schema_file) as f:
                new_schema = json.load(f)
        except (FileNotFoundError, json.JSONDecodeError):
            continue

        if old_schema is None:
            # New schema, not a breaking change
            continue

        issues = check_breaking_changes(old_schema, new_schema)
        if issues:
            all_issues[schema_file.name] = issues

    # Output results
    if not all_issues:
        print("  No breaking changes detected.")
        return 0

    if args.output_format == "github":
        print("## Breaking Changes Detected")
        print("")
        for schema, issues in all_issues.items():
            print(f"### {schema}")
            for issue in issues:
                print(f"- {issue}")
            print("")
    else:
        for schema, issues in all_issues.items():
            print(f"  {schema}:")
            for issue in issues:
                print(f"    - {issue}")

    return 1


if __name__ == "__main__":
    sys.exit(main())
