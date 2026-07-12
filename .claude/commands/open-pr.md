# /open-pr — Review-then-open pull request

Open a pull request for the current branch, but ONLY after a fresh-context code
review. This command is the sanctioned path; `gh pr create` run directly will
be blocked by a hook until a review for the current diff is on record.

Optional args ($ARGUMENTS): extra flags for `gh pr create`, e.g. `--base main`
to target a different base branch. The PR **title** is synthesized by this
command (step 5/6) and the **body** is rendered from `pull_request_template.md`
— never from `--fill`.

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

5. **Compose the PR body from the template.** Read
   `.github/pull_request_template.md` and fill in its four sections from the
   review outcome and the branch diff. **Do NOT use `--fill`** — `--fill` only
   pastes the commit list as the body and ignores the template, which is the bug
   being corrected here. Replace each `<!-- -->` placeholder with real content
   (or drop the comment line entirely):
   - **What changed:** one or two sentences, plus a short bullet list of the
     concrete changes (files / functions touched).
   - **Why:** the motivation — which roadmap item or problem this addresses.
   - **How it was verified:** what ran green (native tests, `pio run`,
     `flash-verify.sh`).
   - **Risks / follow-ups:** anything left open, board-prep steps, or next tasks.
   Tick the relevant `- [ ]` checkboxes under "How it was verified" (or mark
   them N/A). Write the filled body to `.claude/.pr-body.md`. Also pick a
   concise `--title` for the PR (e.g. the branch's primary change / first commit
   subject) and set it as `PR_TITLE` in step 6.

6. **Record the review and open the PR.** The `block-pr-without-review.sh`
   `PreToolUse` hook blocks `gh pr create` unless `.claude/.pr-review-ok` already
   holds the signature of the *current* diff. So write the sentinel in its own
   Bash call first, then open the PR in a second call — do **not** combine them,
   because the hook runs *before* the Bash call executes, so a sentinel written
   inside the same call would not exist yet. Make sure your work is committed
   first (step 1) and is unchanged after writing the sentinel, so the signatures
   match.

   Call A — record the review for this exact diff:
   ```bash
   bash .claude/scripts/pr-review-sig.sh > .claude/.pr-review-ok
   ```

   Call B — open the draft PR (the hook now sees the matching sentinel):
   ```bash
   PR_TITLE="<one-line title — branch's primary change / first commit subject>"
   gh pr create --draft --title "$PR_TITLE" --body-file .claude/.pr-body.md $ARGUMENTS
   ```
   `$ARGUMENTS` carries any extra flags (e.g. `--base main`); do **not** pass
   `--title` there — the command synthesizes it. Never use `--fill`.

7. **Report** the PR URL and a one-line summary of the review outcome (e.g.
   "Fresh review: 3 must-fix addressed, 0 remaining; draft PR opened with
   template body.").
