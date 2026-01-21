# RFD: Hammer Review 5b2ccd6

**Commit:** 5b2ccd6a9c5d49f8c7d9720f886d226662739fbd
**Date:** 2026-01-21
**Author:** Hammer Review Bot

---

## Summary

This commit fixes the `workflow_dispatch` trigger for the Hammer Review workflow that was added in commit 1028bff. The previous implementation would fail on manual runs because:

1. The `[skip-hammer]` condition would incorrectly skip manual runs (no `head_commit.message` exists for `workflow_dispatch`)
2. `github.event.before` is undefined for `workflow_dispatch`, causing the script to fail

### Changes

```diff
diff --git a/.github/workflows/hammer-review.yml b/.github/workflows/hammer-review.yml
index 1e68939..71086ec 100644
--- a/.github/workflows/hammer-review.yml
+++ b/.github/workflows/hammer-review.yml
@@ -10,7 +10,7 @@ on:
 jobs:
   hammer-review:
     if: |
-      !contains(github.event.head_commit.message, '[skip-hammer]')
+      github.event_name == 'workflow_dispatch' || !contains(github.event.head_commit.message, '[skip-hammer]')
     runs-on: ubuntu-latest

@@ -60,7 +60,7 @@ jobs:
       - name: Run hammer review
         env:
           SPRITES_API_TOKEN: ${{ env.SPRITES_API_TOKEN }}
-          BEFORE_SHA: ${{ github.event.before }}
+          BEFORE_SHA: ${{ github.event.before || '0000000000000000000000000000000000000000' }}
           SHA: ${{ github.sha }}
```

---

## Findings

### 1. **Info: Fix correctly addresses previous RFD findings**

**Location:** `.github/workflows/hammer-review.yml:13, 63`

The commit addresses the issues identified in RFD-20260121-1028bff:

- **Condition fix (line 13):** The `if` condition now explicitly allows `workflow_dispatch` events through, bypassing the `[skip-hammer]` check that would fail due to missing `head_commit.message`.

- **BEFORE_SHA fallback (line 63):** Uses `0000000...` as a sentinel value when `github.event.before` is undefined. This aligns with the prompt template in `scripts/hammer-review-prompt.py:24` which instructs the reviewer to use `git show` instead of `git diff` when BEFORE_SHA is all zeros.

---

### 2. **Low Severity: No explicit `workflow_dispatch` inputs**

**Location:** `.github/workflows/hammer-review.yml:8`

**Issue:** The `workflow_dispatch` trigger still lacks explicit inputs for `sha` and `before_sha`. Manual runs will:
- Review the HEAD of the branch at trigger time (`github.sha`)
- Use `0000000...` as the before SHA (triggering single-commit review mode)

**Impact:** Users cannot specify arbitrary commits to review via manual dispatch. This may be intentional design (review current HEAD only) but limits flexibility.

**Mitigation:** The current behavior is functional and safe. Adding inputs would be an enhancement, not a fix.

---

### 3. **Info: Script compatibility verified**

**Location:** `scripts/run-hammer-review.sh:9-12`

The script's validation:
```bash
if [[ -z "${BEFORE_SHA:-}" || -z "${SHA:-}" ]]; then
```

Will pass because `BEFORE_SHA` now has a fallback value of 40 zeros, which is non-empty.

The prompt template (`scripts/hammer-review-prompt.py:24`) already handles this case:
```python
f"   If BEFORE_SHA is 0000000... (new branch), use \"git show {sha}\".\n"
```

---

### 4. **Info: No test coverage for workflow changes**

**Location:** N/A

GitHub Actions workflows are not unit testable. The fix can only be validated by:
1. Manual `workflow_dispatch` trigger
2. Push with `[skip-hammer]` tag (should skip)
3. Push without tag (should run)

---

## Recommended Actions

1. **None required:** The fix correctly addresses the issues from the previous commit. Manual workflow runs should now function as intended.

2. **Optional enhancement:** Add `workflow_dispatch` inputs for custom commit selection:
   ```yaml
   workflow_dispatch:
     inputs:
       sha:
         description: 'Commit SHA to review (defaults to HEAD)'
         required: false
       before_sha:
         description: 'Base SHA for diff (defaults to 0000...)'
         required: false
   ```
   Then update the env section to prefer inputs when provided.

3. **Testing:** Verify the fix works by manually triggering the workflow from GitHub Actions UI.

---

## Conclusion

This commit is a correct and complete fix for the issues identified in the previous RFD. The `workflow_dispatch` trigger is now functional:

- Manual runs bypass the `[skip-hammer]` condition check
- The `BEFORE_SHA` fallback ensures the script doesn't fail on missing context
- The prompt template already handles the all-zeros sentinel value

No blocking issues identified. The commit can be merged safely.
