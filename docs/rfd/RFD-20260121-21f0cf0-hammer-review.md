# RFD: Hammer Review 21f0cf0

**Commit:** 21f0cf023578dcb6d2552552cd89fb7f7f2863ad
**Date:** 2026-01-21
**Author:** Hammer Review Bot

## Summary

This commit fixes a critical bug in the `anvil-review.yml` workflow where the prompt variable was being expanded prematurely on the GitHub runner instead of on the remote sprite. The fix applies proper escaping to ensure the prompt is passed correctly to the `codex exec` command.

**Files Changed:**
- `.github/workflows/anvil-review.yml` (1 line changed)

**Change Details:**

```diff
-          printf '%s' "$PROMPT" | codex exec --dangerously-bypass-approvals-and-sandbox -
+          printf '%s' "\$PROMPT" | codex exec --dangerously-bypass-approvals-and-sandbox -
```

This fix was identified as a critical issue in the previous RFD review (RFD-20260121-eeca86b-hammer-review.md, Finding #1).

## Findings

### 1. LOW: Fix is correct and complete

**Severity:** Low (Positive finding)

**Location:** `.github/workflows/anvil-review.yml:104`

**Description:**

The fix correctly addresses the shell variable expansion issue. In the heredoc context:

1. Line 102: `PROMPT_B64="${PROMPT_B64}"` - The outer `${PROMPT_B64}` is expanded at heredoc creation time (on the GitHub runner), embedding the base64-encoded prompt directly into the script.

2. Line 103: `PROMPT="\$(printf '%s' \"\$PROMPT_B64\" | base64 -d -i)"` - The escaped `\$(...)` defers command substitution to the sprite, where it decodes the embedded base64 data and assigns it to `PROMPT`.

3. Line 104 (fixed): `printf '%s' "\$PROMPT"` - The escaped `\$PROMPT` now correctly defers variable expansion to the sprite, where `PROMPT` has been assigned the decoded prompt value.

Before this fix, `$PROMPT` (unescaped) would expand on the GitHub runner where `PROMPT` was undefined, resulting in an empty string being piped to codex.

**Impact:** The anvil-review workflow should now function correctly, receiving the full prompt content.

### 2. INFO: Remaining issues from previous review

**Severity:** Informational

**Description:**

The previous RFD (eeca86b) identified several other issues that remain unaddressed by this commit:

| Finding | Severity | Status |
|---------|----------|--------|
| Prompt escaping bug | High | **Fixed by this commit** |
| Unused environment variables | Medium | Not addressed |
| Inconsistent branch triggers | Medium | Not addressed |
| Missing GITHUB_TOKEN for git operations | Low | Not addressed |

These remaining issues are lower priority and may be addressed in future commits. The critical functionality blocker has been resolved.

### 3. INFO: No new risks introduced

**Severity:** Informational

**Description:**

This change is purely corrective and introduces no new functionality, dependencies, or security concerns. The fix:
- Does not change the workflow's permissions or secrets handling
- Does not alter the execution flow
- Does not add new external dependencies
- Is a minimal, targeted fix to the identified bug

## Recommended Actions

1. **Verify workflow execution** - Trigger the anvil-review workflow (either via push or workflow_dispatch) to confirm the fix resolves the empty prompt issue.

2. **Consider addressing remaining issues** from the previous review in a follow-up commit:
   - Remove unused `REVIEWER_NAME`, `SKIP_TOKEN`, and `MAX_TURNS` environment variables
   - Align branch triggers with `hammer-review.yml` if desired

3. **Add workflow documentation** - Consider adding a comment in the workflow explaining the heredoc escaping requirements for future maintainers.

## Risk Assessment

**Overall Risk: Low**

This is a well-targeted fix for a previously identified critical bug. The change is minimal (1 character added for escaping), follows established patterns used elsewhere in the codebase (e.g., `hammer-review.yml` line 101 uses `\$PROMPT`), and introduces no new risks.

The workflow should now function as intended, allowing the anvil/codex review system to receive and process the review prompt correctly.
