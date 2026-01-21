#!/usr/bin/env bash
set -euo pipefail

if [[ -z "${SPRITES_API_TOKEN:-}" ]]; then
  echo "SPRITES_API_TOKEN is required." >&2
  exit 1
fi

if [[ -z "${BEFORE_SHA:-}" || -z "${SHA:-}" ]]; then
  echo "BEFORE_SHA and SHA must be set." >&2
  exit 1
fi

SHORT_SHA="${SHORT_SHA:-${SHA:0:7}}"
DATE_UTC="${DATE_UTC:-$(date -u +%Y%m%d)}"
export SHORT_SHA DATE_UTC

PROMPT_B64="$(python scripts/hammer-review-prompt.py)"

sprite auth setup --token "$SPRITES_API_TOKEN"

REMOTE_SCRIPT=$(cat <<EOF
set -euo pipefail
cd /home/sprite/workspace/rs-1
git fetch origin
git checkout ${SHA}
git reset --hard ${SHA}
mkdir -p docs/rfd
PROMPT_B64="${PROMPT_B64}"
PROMPT="\$(printf '%s' "\$PROMPT_B64" | base64 -d)"
claude --print --output-format json --dangerously-skip-permissions --max-turns 30 "\${PROMPT}"
EOF
)

sprite exec -http-post -s hammer /bin/bash -lc "$REMOTE_SCRIPT"
