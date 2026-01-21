# RFD: Hammer Review eeca86b

**Commit:** eeca86b63c99e48aae7d7a83d2c815743cc6d690
**Date:** 2026-01-21
**Author:** Hammer Review Bot

## Summary

This commit introduces a new GitHub Actions workflow `anvil-review.yml` that runs code reviews using OpenAI Codex via the `codex exec` command. The workflow mirrors the existing `hammer-review.yml` (which uses Claude) but targets a different AI agent ("anvil") and uses Codex for review generation.

**Files Changed:**
- `.github/workflows/anvil-review.yml` (+108 lines)

**Key Differences from hammer-review.yml:**
| Aspect | hammer-review.yml | anvil-review.yml |
|--------|-------------------|------------------|
| Sprite Name | `hammer` | `anvil` |
| Skip Token | `[skip-hammer]` | `[skip-anvil]` |
| AI Tool | `claude --print --output-format json --dangerously-skip-permissions` | `codex exec --dangerously-bypass-approvals-and-sandbox` |
| Branch Triggers | `main`, `feature/**` | `main`, `dev`, `feature/**` |
| Prompt Delivery | Via CLI argument | Via stdin pipe |

## Findings

### 1. HIGH: Prompt not properly passed to codex exec

**Severity:** High

**Location:** `.github/workflows/anvil-review.yml:103-104`

**Description:**

The workflow attempts to pass the decoded prompt to `codex exec` via stdin:

```bash
PROMPT="\$(printf '%s' \"\$PROMPT_B64\" | base64 -d -i)"
printf '%s' "$PROMPT" | codex exec --dangerously-bypass-approvals-and-sandbox -
```

However, there's a subtle bug in the heredoc expansion. The `PROMPT` variable assignment uses escaped `\$()` syntax to defer evaluation to the remote sprite, but then `printf '%s' "$PROMPT"` on line 104 uses unescaped `$PROMPT` which will be expanded **on the GitHub runner** before transmission to the sprite.

Since `PROMPT_B64` is defined on line 99 (`PROMPT_B64="${PROMPT_B64}"`), the outer shell expands `${PROMPT_B64}` at heredoc creation time, but the `PROMPT` variable itself is never set in the runner's environment - it's only meant to be set inside the sprite. This means `$PROMPT` on line 104 will be empty or undefined in the runner context.

Compare with hammer-review.yml line 101 which passes the prompt directly to Claude as a command argument:
```bash
claude --print --output-format json --dangerously-skip-permissions --max-turns ${MAX_TURNS} "\$PROMPT"
```

Here `\$PROMPT` is correctly escaped to expand on the sprite.

**Impact:** The codex exec command likely receives an empty prompt, causing the review to fail or produce meaningless output.

**Recommendation:** Escape the `$PROMPT` reference on line 104:
```bash
printf '%s' "\$PROMPT" | codex exec --dangerously-bypass-approvals-and-sandbox -
```

### 2. MEDIUM: Unused environment variables defined

**Severity:** Medium

**Location:** `.github/workflows/anvil-review.yml:17-19`

**Description:**

The workflow defines these environment variables that are never used:

```yaml
env:
  SPRITE_NAME: anvil
  REVIEWER_NAME: anvil    # Never referenced
  SKIP_TOKEN: skip-anvil  # Never referenced
  MAX_TURNS: 30           # Never referenced
```

The `hammer-review.yml` also defines `MAX_TURNS` and uses it on line 101, but `anvil-review.yml` doesn't pass `MAX_TURNS` to codex. This may be intentional (codex may not support this flag), but the variable is still defined without use.

`REVIEWER_NAME` and `SKIP_TOKEN` appear to be metadata that isn't used in the workflow logic.

**Impact:** Low - cosmetic/maintenance issue. However, the absence of `MAX_TURNS` usage may indicate missing functionality if codex supports turn limits.

**Recommendation:** Either use these variables or remove them to reduce confusion.

### 3. MEDIUM: Inconsistent branch triggers

**Severity:** Medium

**Location:** `.github/workflows/anvil-review.yml:5-8`

**Description:**

The anvil workflow triggers on:
```yaml
branches:
  - main
  - dev
  - "feature/**"
```

While hammer-review.yml triggers on:
```yaml
branches:
  - main
  - "feature/**"
```

Adding `dev` branch is not necessarily wrong, but the inconsistency between the two review workflows means:
- Commits to `dev` will trigger anvil reviews but not hammer reviews
- This may or may not be intentional behavior

**Impact:** Inconsistent review coverage across branches could lead to confusion about which reviews apply where.

**Recommendation:** Clarify if this is intentional. If both workflows should have the same triggers, align them.

### 4. LOW: Missing GITHUB_TOKEN export for authenticated git operations

**Severity:** Low

**Location:** `.github/workflows/anvil-review.yml:64`

**Description:**

Line 64 sets:
```bash
echo "AGENT_HARNESS_TOKEN=${GITHUB_TOKEN}" >> "$GITHUB_ENV"
```

This token is used for fetching the prompt from the agent-harness repo (line 78), but the sprite remote script uses unauthenticated `git clone`:
```bash
git clone https://github.com/${REPO_FULL}.git "${REPO_NAME}"
```

If the repo is private or requires authentication for pushing commits (which the review workflow likely does after creating RFD files), the git operations may fail.

The hammer-review.yml has the same pattern, so this may be a known limitation or the repos are public.

**Impact:** Git operations requiring authentication would fail on the sprite. Since this appears to work for hammer-review, it's likely acceptable.

**Recommendation:** Document whether authenticated git operations are expected to work on the sprite, or if results are pushed via a different mechanism.

### 5. INFO: Different prompt delivery mechanism

**Severity:** Informational

**Location:** `.github/workflows/anvil-review.yml:104`

**Description:**

The workflow uses stdin for prompt delivery (`printf | codex exec -`), while hammer uses a command-line argument. This is a reasonable architectural choice based on how each tool accepts input, but:

1. Stdin delivery avoids command-line length limits for large prompts
2. However, piping through printf adds an extra process and potential for encoding issues

The `-` at the end of `codex exec` indicates reading from stdin, which is the correct invocation pattern.

### 6. INFO: No test coverage for new workflow

**Severity:** Informational

**Description:**

This is a new workflow file with no associated tests. Workflow testing is inherently difficult, but the workflow will only be validated when:
1. A push to main/dev/feature/** occurs without `[skip-anvil]`
2. A manual workflow_dispatch is triggered

The `[skip-anvil]` token allows bypassing for testing purposes.

## Recommended Actions

1. **CRITICAL: Fix the prompt escaping bug** (Finding #1)
   Change line 104 from:
   ```bash
   printf '%s' "$PROMPT" | codex exec --dangerously-bypass-approvals-and-sandbox -
   ```
   To:
   ```bash
   printf '%s' "\$PROMPT" | codex exec --dangerously-bypass-approvals-and-sandbox -
   ```

2. **Clean up unused variables** (Finding #2)
   Remove `REVIEWER_NAME`, `SKIP_TOKEN`, and `MAX_TURNS` if they serve no purpose, or document their intended use.

3. **Align branch triggers** (Finding #3)
   Decide if both workflows should trigger on the same branches and update accordingly.

4. **Add inline comments** explaining the differences between anvil and hammer workflows for future maintainers.

5. **Test the workflow** manually via workflow_dispatch before relying on automatic triggers.

## Risk Assessment

**Overall Risk: High**

The critical prompt escaping bug (Finding #1) means this workflow is unlikely to function correctly as written. The prompt variable will be empty in the sprite execution context, causing codex to receive no input or fail.

Until this is fixed, the workflow should be considered non-functional. The fix is straightforward (escaping `$PROMPT`), but the workflow needs testing after the fix is applied.
