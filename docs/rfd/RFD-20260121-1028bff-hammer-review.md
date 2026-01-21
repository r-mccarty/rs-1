# RFD: Hammer Review 1028bff

**Commit:** 1028bff511e1b8197ff06e011c1c54c63e007717
**Date:** 2026-01-21
**Author:** Hammer Review Bot

---

## Summary

This commit adds `workflow_dispatch` trigger to the `.github/workflows/hammer-review.yml` workflow, enabling manual runs of the Hammer Review workflow from the GitHub Actions UI.

### Change

```diff
diff --git a/.github/workflows/hammer-review.yml b/.github/workflows/hammer-review.yml
index 2500b1a..1e68939 100644
--- a/.github/workflows/hammer-review.yml
+++ b/.github/workflows/hammer-review.yml
@@ -5,6 +5,7 @@ on:
     branches:
       - main
       - "feature/**"
+  workflow_dispatch:
```

---

## Findings

### 1. **Medium Severity: Missing `BEFORE_SHA` in `workflow_dispatch` context**

**Location:** `.github/workflows/hammer-review.yml:63` and `scripts/run-hammer-review.sh:9-12`

**Issue:** When the workflow is triggered via `workflow_dispatch`, `github.event.before` is **undefined** (empty string). The script `run-hammer-review.sh` requires `BEFORE_SHA` to be set and will fail with:

```
BEFORE_SHA and SHA must be set.
```

This means manual workflow runs will fail immediately.

**Evidence from workflow:**
```yaml
env:
  BEFORE_SHA: ${{ github.event.before }}   # Empty for workflow_dispatch
  SHA: ${{ github.sha }}
```

**Evidence from script:**
```bash
if [[ -z "${BEFORE_SHA:-}" || -z "${SHA:-}" ]]; then
  echo "BEFORE_SHA and SHA must be set." >&2
  exit 1
fi
```

**Impact:** The intended feature (manual runs) does not work as implemented.

---

### 2. **Low Severity: No inputs defined for `workflow_dispatch`**

**Location:** `.github/workflows/hammer-review.yml:8`

**Issue:** The `workflow_dispatch` trigger is added without any inputs. For manual runs to be useful, users should be able to specify:
- A target commit SHA to review
- Optionally, a base SHA for comparison

**Example of what would make this functional:**
```yaml
workflow_dispatch:
  inputs:
    sha:
      description: 'Commit SHA to review'
      required: true
    before_sha:
      description: 'Base SHA for diff comparison'
      required: false
      default: ''
```

---

### 3. **Info: No test coverage for workflow changes**

**Location:** N/A

**Note:** GitHub Actions workflow files are not typically unit tested, but manual verification of the workflow behavior would be advisable before merging. The missing `BEFORE_SHA` issue would be caught on first manual run attempt.

---

## Recommended Actions

1. **Required:** Add `workflow_dispatch` inputs for `sha` and `before_sha` parameters.

2. **Required:** Update workflow to use input values when triggered manually:
   ```yaml
   env:
     BEFORE_SHA: ${{ github.event.inputs.before_sha || github.event.before }}
     SHA: ${{ github.event.inputs.sha || github.sha }}
   ```

3. **Required:** Update `scripts/run-hammer-review.sh` to handle empty `BEFORE_SHA` gracefully (e.g., use `git show` instead of `git diff` when `BEFORE_SHA` is empty, as the prompt already suggests).

4. **Optional:** Add documentation for how to use manual workflow dispatch.

---

## Conclusion

The commit adds the `workflow_dispatch` trigger but lacks the necessary inputs and fallback logic to make manual runs functional. Manual workflow runs will fail immediately due to the missing `BEFORE_SHA` environment variable. This is a partial implementation that requires follow-up work to be useful.
