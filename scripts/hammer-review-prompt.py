#!/usr/bin/env python3
import base64
import os
import sys


def required_env(name: str) -> str:
    value = os.environ.get(name)
    if not value:
        sys.stderr.write(f"Missing required env var: {name}\n")
        sys.exit(1)
    return value


before_sha = required_env("BEFORE_SHA")
sha = required_env("SHA")
short_sha = required_env("SHORT_SHA")
date_utc = required_env("DATE_UTC")

prompt = (
    f"You are reviewing commit {sha} in /home/sprite/workspace/rs-1.\n\n"
    "Task:\n"
    f"1) Check out the repo at the commit SHA and inspect the diff from {before_sha}..{sha}.\n"
    f"   If BEFORE_SHA is 0000000... (new branch), use \"git show {sha}\".\n"
    "2) Perform an in-depth code review focused on bugs, behavior regressions, risks,\n"
    "   and missing tests. Prioritize critical issues.\n"
    f"3) Create a new RFD file at docs/rfd/RFD-{date_utc}-{short_sha}-hammer-review.md with:\n"
    f"   - Title: \"RFD: Hammer Review {short_sha}\"\n"
    f"   - Commit: {sha}\n"
    "   - Summary\n"
    "   - Findings (ordered by severity)\n"
    "   - Recommended actions\n"
    "4) Commit and push the RFD with message:\n"
    f"   \"RFD: hammer review {short_sha} [skip-hammer]\"\n\n"
    "Constraints:\n"
    "- Keep changes limited to the RFD file.\n"
    "- Do not modify unrelated files.\n"
    "- Use git diff or git show to reference specific files/lines where possible.\n"
)

print(base64.b64encode(prompt.encode("utf-8")).decode("utf-8"))
