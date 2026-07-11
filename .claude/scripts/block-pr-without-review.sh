#!/usr/bin/env bash
# PreToolUse hook for Bash commands matching "gh pr create".
#
# Blocks `gh pr create` unless a fresh code review has been recorded for the
# EXACT current diff. The review is recorded by the `/open-pr` command, which
# first runs /code-review in a fresh subagent (new context), applies must-fix
# changes, then writes the sentinel file below.
#
# If the diff changes after a review, the signature no longer matches and the
# PR is blocked again — forcing a re-review. This is intentional.
#
# Note: this hook is an aid, not a hard security boundary. The matcher regex
# can be bypassed (e.g. `gh  pr create` with double spaces, or via `sh -c`).
# The real guarantee is the /open-pr workflow in AGENTS.md.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(git rev-parse --show-toplevel)"
SENTINEL="$REPO_ROOT/.claude/.pr-review-ok"

CURRENT="$("$SCRIPT_DIR/pr-review-sig.sh")"
CUR_BRANCH="$(jq -r .branch <<<"$CURRENT")"
CUR_SIG="$(jq -r .diffSig <<<"$CURRENT")"

if [[ -f "$SENTINEL" ]]; then
  STORED_BRANCH="$(jq -r .branch "$SENTINEL" 2>/dev/null || true)"
  STORED_SIG="$(jq -r .diffSig "$SENTINEL" 2>/dev/null || true)"
  if [[ "$STORED_BRANCH" == "$CUR_BRANCH" && "$STORED_SIG" == "$CUR_SIG" ]]; then
    # Fresh review recorded for this exact diff -> allow the PR creation.
    exit 0
  fi
fi

# Deny. Build the JSON with jq so a quote/newline in the reason can't corrupt it,
# and exit non-zero so the call is blocked even if the harness only checks exit code.
REASON="Pull request creation is blocked until a fresh code review is recorded for the current diff. Run the /open-pr command: it spawns a fresh subagent to run /code-review, applies must-fix changes, records the review, then opens a draft PR. Do not run \`gh pr create\` directly."

jq -n --arg r "$REASON" '{hookSpecificOutput:{hookEventName:"PreToolUse",permissionDecision:"deny",permissionDecisionReason:$r}}'
exit 1
