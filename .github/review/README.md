# Review agent

[`review.yml`](../workflows/review.yml) runs Claude Code on every push and checks
the change against [RUBRIC.md](RUBRIC.md), which condenses the Group 4 plan and
CONTRACTS.md. Each review produces:

- a verdict: **Merge**, **Merge after fixes** or **Don't merge**;
- a report covering what the author built, how it lines up with the plan, problems
  with file:line references, and what to fix before merging;
- the report as a comment on the branch's open pull request (updated in place on
  later pushes), in the Actions run summary, and by email to the repository owner.

The agent is advisory. The plan's peer-review rotation still applies.

## When it runs

| Push | Reviewed against |
| --- | --- |
| Any branch other than `main` | Everything the branch adds since it left `main` |
| `main`, by someone other than the owner, not from a merged PR | The commits in that push, flagged as bypassing review |

It skips the owner's own pushes to `main`, merges of PRs (the branch was already
reviewed), bot pushes, branch deletions and pushes whose last commit message
contains `[skip review]`. A newer push to the same branch cancels a review that is
still running, so quick successive pushes produce one report.

To review a branch on demand (works once this workflow is on `main`):
`gh workflow run review.yml --ref <branch>`. To redo a review: `gh run rerun <run-id>`.

## One-time setup (owner)

Run these from the repository folder. Each `gh secret set` prompts for the value,
so secrets never land in shell history.

1. **Claude access.** Pick one:
   - Claude Pro or Max: run `claude setup-token`, then `gh secret set CLAUDE_CODE_OAUTH_TOKEN`
     and paste the token. Reviews count against your plan's usage.
   - API key from console.anthropic.com: `gh secret set ANTHROPIC_API_KEY`.
     Reviews are billed per token.
2. **Email.** The workflow sends mail over SMTP, Gmail by default:
   - Turn on 2-Step Verification for a Gmail account, then create an app password
     at https://myaccount.google.com/apppasswords.
   - `gh secret set MAIL_USERNAME` with the Gmail address.
   - `gh secret set MAIL_PASSWORD` with the 16-character app password.
   - `gh secret set REVIEW_EMAIL_TO` with the address that should receive reports.
   - For another provider, also run `gh variable set MAIL_SERVER` and
     `gh variable set MAIL_PORT` (465 for TLS).

   Without these secrets, reviews still run and post to the PR and the run summary.
   The run then shows a notice that email was skipped.
3. **Optional model choice.** Reviews use `claude-opus-5-5`. To trade depth for
   speed and usage: `gh variable set REVIEW_MODEL --body claude-sonnet-5-5`.

The workflow file has to be on `main` for branches created from `main` to carry it.

## Things to know

- The repository is public, so PR comments and run summaries are visible to anyone
  who can see the repository.
- Anyone with write access can edit workflows, and so can reach these secrets.
  Only add the four team members as collaborators.
- Changes to `.github/` in a branch, including this rubric, are flagged in the
  report for the owner's attention.
- The reviewer runs the Claude Code CLI with a fixed tool allowlist and ignores a
  branch's own `.claude/` settings and `.mcp.json`, so pushed code can't widen
  what it is allowed to do.
