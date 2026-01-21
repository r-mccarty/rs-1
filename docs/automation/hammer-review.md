# Hammer Review Automation

On every push, GitHub Actions triggers `hammer-review.yml` to run an automated
code review on the `hammer` sprite. The workflow uses Claude Code to analyze the
commit diff and writes an RFD describing risks and concerns.

## Workflow

- Trigger: every `push` to `main` and `feature/**`
- Guard: skips commits containing `[skip-hammer]`
- Runner: `sprite exec -http-post` on `hammer`
- Output: `docs/rfd/RFD-YYYYMMDD-<shortsha>-hammer-review.md`
- Commit: `RFD: hammer review <shortsha> [skip-hammer]`

## Secrets

The workflow loads secrets via Infisical:

- `INFISICAL_TOKEN`
- `INFISICAL_PROJECT_ID`
- `INFISICAL_ENV` (optional, defaults to `dev`)

`SPRITES_API_TOKEN` must be present in Infisical so the workflow can authenticate
with the Sprites API.

## Troubleshooting

- If the workflow fails with `sprite exec` errors, ensure `hammer` exists and is
  provisioned.
- If Claude hangs, confirm the workflow is using the HTTP POST exec path.

Trigger test: 2026-01-21T05:42:42Z
