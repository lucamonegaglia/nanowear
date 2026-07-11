#!/usr/bin/env bash
# Compute a stable signature of the current branch's reviewable diff.
# Outputs JSON: {"branch": <name>, "diffSig": <sha256>}
#
# The signature covers everything a reviewer would care about:
#   - committed changes since the branch forked off main
#   - unstaged working-tree changes
#   - staged (cached) changes
# so that any edit made after a review invalidates a previously recorded review.
set -euo pipefail

BRANCH="$(git rev-parse --abbrev-ref HEAD)"

# Fork point: prefer local 'main', then 'origin/main'. If neither exists, fail
# closed with a clear message rather than letting `git diff main HEAD` blow up
# under `set -e` (which would block EVERY `gh pr create` via the calling hook).
if BASE="$(git merge-base main HEAD 2>/dev/null)"; then
  :
elif BASE="$(git merge-base origin/main HEAD 2>/dev/null)"; then
  :
else
  echo "pr-review-sig: cannot find 'main' or 'origin/main' to diff against." >&2
  exit 1
fi

# Portable sha256 (sha256sum on GNU/Linux, shasum -a 256 on macOS).
sha256() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256
  else
    echo "pr-review-sig: no sha256 tool (sha256sum/shasum) found." >&2
    exit 1
  fi
}

DIFF_SIG="$( { git diff "$BASE" HEAD; git diff; git diff --cached; } | sha256 | cut -d' ' -f1 )"

jq -n --arg branch "$BRANCH" --arg diffSig "$DIFF_SIG" '{branch:$branch, diffSig:$diffSig}'
