# /open-pr — Review-then-open pull request

Open a pull request for the current branch, but ONLY after a fresh-context code
review. This command is the sanctioned path; `gh pr create` run directly will
be blocked by a hook until a review for the current diff is on record.

Optional args ($ARGUMENTS): extra flags/title for `gh pr create`, e.g.
`--title "Add IMU init"`. Leave empty for `--fill` from the branch/commits.

## Steps

1. **Guardrails.** Confirm you are NOT on `main`. If you are, stop and tell the
   user to switch to a feature branch first. **Commit** your work so the diff is
   complete and the reviewed scope equals the scope that gets opened.

2. **Fresh-context review.** Spawn a FRESH subagent (via the Agent tool) to
   review the branch. The subagent starts with clean context — it has NOT seen
   the coding conversation, so it reviews without author bias. Instruct it:
   - Run `/code-review` on the current branch's changes vs `main`.
   - Report a concise, prioritized list: **must-fix** (correctness bugs, build
     breakage, standard violations from AGENTS.md) vs **nice-to-have**.
   - Do NOT edit files; only report.

3. **Address must-fix findings.** Apply every must-fix change yourself. Keep the
   conversation's non-blocking / commented style from AGENTS.md. Build to verify:
   `pio run -e nanorp2040connect` (or `pio test -e native`). Re-run the build
   after fixes so the tree is green.

4. **Confirm (recommended).** Spawn the fresh review subagent again to confirm
   the must-fix list is now empty. If it still finds must-fix items, go to step 3.

5. **Record the review and open the PR atomically.** Run this in ONE Bash call so
   the diff signature is computed at the same instant the PR is opened (nothing
   can change in between, which would make the hook block you). Make sure your
   work is committed first (step 1) so the reviewed scope equals the opened scope.

   ```bash
   bash .claude/scripts/pr-review-sig.sh > .claude/.pr-review-ok \
     && gh pr create --draft --fill "$ARGUMENTS"
   ```

   The sentinel satisfies the `gh pr create` hook for this exact diff.

6. **Report** the PR URL and a one-line summary of the review outcome (e.g.
   "Fresh review: 3 must-fix addressed, 0 remaining; draft PR opened.").
