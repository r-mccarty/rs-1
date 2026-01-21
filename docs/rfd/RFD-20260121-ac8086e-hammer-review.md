# RFD: Hammer Review ac8086e

**Commit:** ac8086e0fea0edd1824df83f42bffe262643691e
**Date:** 2026-01-21
**Author:** Hammer Review Bot

## Summary

This commit indents the Python and bash heredoc content within the `hammer-review.yml` GitHub Actions workflow. The change was intended to improve code readability by aligning the heredoc contents with the surrounding YAML indentation.

**Files Changed:**
- `.github/workflows/hammer-review.yml`

## Findings

### 1. CRITICAL: Heredoc delimiters are broken due to indentation

**Severity:** Critical (workflow will fail to execute)

**Location:** `.github/workflows/hammer-review.yml:109` and `.github/workflows/hammer-review.yml:133`

**Description:**

Bash heredocs require the closing delimiter to appear at the start of a line (column 0) unless using the `<<-` syntax (which only strips leading **tabs**, not spaces). This commit indents the `PY` delimiter (line 109) and `EOF` delimiter (line 133) with spaces:

```diff
-PY
-)"
+          PY
+          )"
```

```diff
-EOF
-)
+          EOF
+          )
```

When bash encounters these indented delimiters, it will not recognize them as the end of the heredoc. Instead, bash will continue reading until EOF or find a `PY`/`EOF` at column 0, causing:

1. A "here-document delimited by end-of-file" warning
2. Syntax errors due to unmatched parentheses
3. Complete workflow failure

**Verification:**

```bash
# This fails with syntax error
test='PROMPT="$(python - <<'"'"'PY'"'"'
          print("test")
          PY
          )"'
bash -c "$test"
# Error: warning: here-document at line 1 delimited by end-of-file (wanted `PY')
```

### 2. CRITICAL: Python IndentationError from heredoc content

**Severity:** Critical (workflow will fail to execute)

**Location:** `.github/workflows/hammer-review.yml:78-108`

**Description:**

Even if the heredoc delimiter issue were resolved, the Python code would fail with an `IndentationError` because the `import` statement and all subsequent lines are now indented with leading spaces. Python requires consistent indentation, and module-level statements (like `import base64`) must start at column 0.

```diff
-import base64
-import os
+          import base64
+          import os
```

When this is fed to `python -`, Python will raise:

```
IndentationError: unexpected indent
```

### 3. LOW: Remote script indentation passed to sprite exec

**Severity:** Low (cosmetic, but relies on bash stripping)

**Location:** `.github/workflows/hammer-review.yml:117-132`

**Description:**

The `REMOTE_SCRIPT` variable now contains leading spaces on every line. While bash will preserve these spaces in the string, the script will still execute correctly because bash ignores leading whitespace in commands. However, this makes debugging harder if the script content is logged, and the extra whitespace is passed to `sprite exec`.

## Recommended Actions

1. **Revert this commit immediately.** The workflow is non-functional in its current state. Both heredocs will fail:
   - The `PY` heredoc delimiter won't be recognized
   - Even if it were, Python would fail with IndentationError
   - The `EOF` heredoc delimiter won't be recognized

2. **Alternative approach if readability is desired:** Use `<<-` with tabs (not spaces) for indentation, but note:
   - `<<-` only strips leading **tabs**, not spaces
   - YAML editors often convert tabs to spaces, making this fragile
   - The Python code still cannot be indented due to Python syntax requirements

3. **Best practice:** Keep heredoc content at column 0 (original format). While it looks visually inconsistent with YAML indentation, it is the only reliable approach for:
   - Bash heredocs with space-based delimiters
   - Python code that must have no leading indentation

## Test Plan

After reverting:

1. Run the workflow manually via `workflow_dispatch` to confirm it completes
2. Verify the Python heredoc produces valid base64 output
3. Verify the remote script executes without syntax errors
