# RFD: Anvil Review 21f0cf0

**Commit:** 21f0cf023578dcb6d2552552cd89fb7f7f2863ad
**Date:** 2026-01-21
**Author:** Anvil Review Bot

## Summary

This commit fixes the anvil review workflow to pass the decoded prompt into Codex on the remote runner by preserving the `$PROMPT` variable for remote expansion.

**Files Changed:**
- `.github/workflows/anvil-review.yml`

## Findings

No findings. The change is narrowly scoped and corrects variable expansion in the remote script.

## Recommended Actions

No additional actions required.
