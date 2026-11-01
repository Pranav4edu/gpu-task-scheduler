#!/bin/sh
# Turns the agent's output into the job summary, PR comment and email subject.
# Inputs (env): BRANCH, MODE, HEAD_SHA, AUTHORS, PR_NUMBER, BUILD_RESULT,
#   CLAUDE_OUTCOME, RUN_URL, MAIL_READY.
# Step outputs: verdict, subject.
set -eu

out=_review/out
report=$out/report.md
mkdir -p "$out"

verdict=$(head -n 1 "$out/verdict.txt" 2>/dev/null | tr -d '[:space:]' || true)
if [ ! -s "$report" ]; then
    verdict=REVIEW_FAILED
    cat > "$report" <<EOF
# Review failed · \`$BRANCH\`

The review agent didn't produce a report (Claude step: $CLAUDE_OUTCOME). On a
first run, check that the \`CLAUDE_CODE_OAUTH_TOKEN\` or \`ANTHROPIC_API_KEY\`
repository secret is set.

- ./run.sh: $BUILD_RESULT
- Commit authors: $AUTHORS
- Logs: $RUN_URL
EOF
fi

case $MODE:$verdict in
    branch:MERGE) label="Merge" ;;
    branch:MERGE_AFTER_FIXES) label="Merge after fixes" ;;
    branch:DO_NOT_MERGE) label="Don't merge" ;;
    direct-to-main:MERGE) label="Keep (pushed straight to main)" ;;
    direct-to-main:MERGE_AFTER_FIXES) label="Fix on main (pushed straight to main)" ;;
    direct-to-main:DO_NOT_MERGE) label="Revert (pushed straight to main)" ;;
    *:REVIEW_FAILED) label="Review failed" ;;
    *) label="No verdict"; verdict=UNKNOWN ;;
esac

short_sha=$(printf '%s' "$HEAD_SHA" | cut -c 1-7)
printf '\n---\n<sub>Automated review of `%s` at %s. Advisory only: a teammate still reviews before merging. [Run log](%s)</sub>\n' \
    "$BRANCH" "$short_sha" "$RUN_URL" >> "$report"

cat "$report" >> "$GITHUB_STEP_SUMMARY"

if [ -n "$PR_NUMBER" ]; then
    gh pr comment "$PR_NUMBER" --body-file "$report" --edit-last --create-if-none \
        || gh pr comment "$PR_NUMBER" --body-file "$report" \
        || echo "::warning::Could not comment on pull request #$PR_NUMBER"
fi

if [ "$MAIL_READY" != true ]; then
    echo "::notice::Email not sent: set the MAIL_USERNAME, MAIL_PASSWORD and REVIEW_EMAIL_TO secrets (see .github/review/README.md)."
fi

printf 'verdict=%s\nsubject=%s\n' "$verdict" "[Group 4 review] $label: $BRANCH by $AUTHORS" >> "$GITHUB_OUTPUT"
