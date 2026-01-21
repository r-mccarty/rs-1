# RFD: Hammer Review 323e285

**Commit:** 323e2856f1f4b1db056c1865fa5aedb5ffe05b79
**Date:** 2026-01-21
**Author:** Hammer Review Bot

## Summary

This commit adds the `-i` (ignore-garbage) flag to the `base64 -d` command in the hammer review workflow. The change is intended to make base64 decoding more tolerant of non-alphabet characters that may appear in the encoded prompt data.

**Files Changed:**
- `.github/workflows/hammer-review.yml`

**Diff:**
```diff
-          PROMPT="\$(printf '%s' \"\$PROMPT_B64\" | base64 -d)"
+          PROMPT="\$(printf '%s' \"\$PROMPT_B64\" | base64 -d -i)"
```

## Context

This commit is part of a series of fixes addressing issues with the hammer review workflow's prompt handling:

1. `63b0518` - avoid local prompt expansion
2. `40335cd` - normalize hammer prompt base64
3. `c037fb5` - fetch hammer prompt via api
4. `9977239` - use raw prompt fetch
5. `323e285` - **this commit**: tolerate prompt base64 noise

The `-i` flag tells GNU coreutils `base64` to ignore non-alphabet characters when decoding. This can help when:
- The base64 string contains accidental whitespace or newlines
- Transmission artifacts introduce non-base64 characters
- The `tr -d '\n'` on line 79 doesn't fully sanitize the input

## Findings

### 1. LOW: Masking of legitimate encoding errors

**Severity:** Low

**Location:** `.github/workflows/hammer-review.yml:100`

**Description:**

The `-i` (ignore-garbage) flag silently discards non-base64 characters during decoding. While this improves robustness against minor input contamination, it also masks potentially significant encoding errors:

- If the upstream `build_prompt.py` script produces malformed base64 due to a bug, the workflow will now silently produce garbled output instead of failing with a clear error
- Truncated or corrupted base64 data may decode to partial content without warning
- The decoded prompt could be subtly wrong (missing characters/content) while appearing to succeed

This is a tradeoff: the fix improves tolerance for benign noise but reduces error visibility.

**Recommendation:** Consider adding validation of the decoded prompt (e.g., checking for expected structure or minimum length) after decoding to catch corruption that `-i` might mask.

### 2. INFO: Platform compatibility consideration

**Severity:** Informational

**Location:** `.github/workflows/hammer-review.yml:100`

**Description:**

The `-i` flag is specific to GNU coreutils `base64`. The workflow runs in two environments:

1. **GitHub Actions runner (ubuntu-latest):** Uses GNU coreutils - `-i` flag is supported
2. **Sprite remote execution:** The `base64 -d -i` command runs on the sprite VM, which also appears to be Linux-based (`/home/sprite/workspace`)

Both environments should support this flag, but if the sprite execution environment ever changed to BSD/macOS (which uses a different `base64` implementation), the `-i` flag would cause an error.

This is unlikely given the current infrastructure but worth noting for documentation purposes.

### 3. INFO: Root cause remains unaddressed

**Severity:** Informational

**Description:**

This fix tolerates "noise" in the base64 data but doesn't identify or address the root cause of the noise. The series of commits suggests ongoing difficulties with prompt encoding/transmission:

| Commit | Description |
|--------|-------------|
| 63b0518 | Avoid local prompt expansion (shell interpretation issues) |
| 40335cd | Normalize hammer prompt base64 |
| c037fb5 | Fetch hammer prompt via API |
| 9977239 | Use raw prompt fetch |
| 323e285 | Tolerate prompt base64 noise |

Each fix addresses a symptom. The underlying issue may be related to:
- Shell quoting/escaping when embedding base64 in YAML/heredoc
- Character encoding during HTTP fetch
- Environment variable expansion contaminating the data

A more robust long-term solution might be to store the prompt in a file rather than inline in environment variables/heredocs.

## Recommended Actions

1. **Accept this change as a reasonable short-term fix.** The `-i` flag is appropriate for improving resilience against minor transmission artifacts.

2. **Monitor for silent failures.** Add logging or validation to detect if the decoded prompt is significantly shorter or malformed compared to expectations.

3. **Consider root cause investigation.** If prompt-related fixes continue to be needed, investigate the source of the base64 contamination to address it at the origin rather than at decode time.

4. **Document the flag.** Add a brief comment explaining why `-i` is used, so future maintainers understand it's intentional:
   ```bash
   # -i: ignore non-base64 chars that may leak from shell/env processing
   PROMPT="\$(printf '%s' \"\$PROMPT_B64\" | base64 -d -i)"
   ```

## Test Plan

1. Verify the workflow completes successfully with the new flag
2. Confirm the decoded prompt content is correct and complete
3. Intentionally introduce a small non-base64 character into test input to verify the flag handles it gracefully
4. Verify the prompt produces expected RFD output (validates decoded content is usable)

## Risk Assessment

**Overall Risk: Low**

This is a defensive fix with minimal downside. The `-i` flag is widely supported on Linux systems and addresses a practical issue with prompt transmission. The main concern is that it may mask future encoding bugs, but this is acceptable given the workflow's iterative nature and visibility into outputs.
