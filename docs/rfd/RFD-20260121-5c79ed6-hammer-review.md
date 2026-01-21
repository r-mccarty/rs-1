# RFD: Hammer Review 5c79ed6

**Commit:** 5c79ed6698741d62ee34476ab99c500644f12793
**Date:** 2026-01-21
**Author:** Hammer Review Bot

## Summary

This commit adds a stale git lock file cleanup step to the `anvil-review.yml` workflow. Before performing git fetch operations, the workflow now checks for and removes `.git/index.lock` if present. This addresses potential issues where a previously interrupted git operation could leave behind a stale lock file, causing subsequent git commands to fail.

**Files Changed:**
- `.github/workflows/anvil-review.yml` (+3 lines)

**Change Details:**

```diff
          cd "/home/sprite/workspace/${REPO_NAME}"
+          if [ -f ".git/index.lock" ]; then
+            rm -f .git/index.lock
+          fi
          git fetch origin
```

The lock cleanup is added between the `cd` into the repository and the `git fetch origin` command (lines 95-97).

## Findings

### 1. LOW: Potential race condition with concurrent workflows

**Severity:** Low

**Location:** `.github/workflows/anvil-review.yml:95-97`

**Description:**

The lock file removal could theoretically interfere with a legitimately running git operation from a concurrent process. However, this is mitigated by:

1. The workflow has `cancel-in-progress: false` set (line 17), which prevents concurrent runs on the same ref.
2. The `concurrency.group` is scoped to `anvil-review-${{ github.ref }}`, so different branches can run simultaneously but the same branch cannot.
3. Sprite environments are typically isolated, making concurrent git operations unlikely.

**Impact:** Minimal. The concurrency controls already in place should prevent legitimate conflicts.

**Recommendation:** The current implementation is acceptable. If issues arise, consider adding a short delay or retry logic rather than immediate removal.

### 2. INFO: Redundant file existence check

**Severity:** Informational

**Location:** `.github/workflows/anvil-review.yml:95-97`

**Description:**

The `rm -f` command already handles the case where the file doesn't exist (the `-f` flag suppresses errors for non-existent files). The `if [ -f ... ]` check is therefore redundant but harmless.

```bash
# Current (with check)
if [ -f ".git/index.lock" ]; then
  rm -f .git/index.lock
fi

# Equivalent (without check)
rm -f .git/index.lock
```

**Impact:** None. The explicit check provides clarity about intent and is a valid defensive coding style.

**Recommendation:** Optional - could simplify to just `rm -f .git/index.lock` for brevity, but the current approach is equally valid and more explicit.

### 3. INFO: Lock removal doesn't address all git locks

**Severity:** Informational

**Location:** `.github/workflows/anvil-review.yml:95-97`

**Description:**

Git can create multiple types of lock files:
- `.git/index.lock` - staging area operations (addressed by this change)
- `.git/refs/heads/<branch>.lock` - branch operations
- `.git/config.lock` - config changes
- `.git/HEAD.lock` - HEAD operations

The fix only addresses `index.lock`. Other lock files could theoretically cause failures, though `index.lock` is by far the most common culprit.

**Impact:** Low. The `index.lock` is the most commonly encountered stale lock file. Other lock types are less likely to be left stale.

**Recommendation:** Monitor workflow failures. If other lock types cause issues, consider a broader cleanup pattern like `find .git -name "*.lock" -delete`.

### 4. INFO: No new risks introduced

**Severity:** Informational

**Description:**

This change:
- Does not alter workflow permissions or secrets handling
- Does not add new external dependencies
- Does not change the core execution logic
- Is a targeted defensive fix for a known failure mode

The change follows the principle of making the workflow more robust against environmental issues in the sprite workspace.

## Recommended Actions

1. **Monitor workflow runs** - Verify this fix resolves any stale lock file issues that may have been occurring.

2. **Consider broader lock cleanup** - If other lock file types cause issues, expand the cleanup:
   ```bash
   find .git -name "*.lock" -type f -delete 2>/dev/null || true
   ```

3. **Optional simplification** - The conditional check could be simplified to just `rm -f .git/index.lock` since `-f` handles missing files.

## Risk Assessment

**Overall Risk: Low**

This is a defensive improvement that handles a potential error condition without introducing new risks. The change:

- Adds robustness against stale lock files from interrupted operations
- Has no negative impact on normal operation
- Is a common pattern for CI/CD git operations
- Is protected from race conditions by existing concurrency controls

The workflow should now be more resilient to transient failures caused by stale git locks.
