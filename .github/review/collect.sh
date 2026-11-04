#!/bin/sh
# Gathers what a push changed into _review/context/ for the review agent.
# Inputs (env): BRANCH, DEFAULT_BRANCH, BEFORE, ACTOR, OWNER, GITHUB_REPOSITORY.
# Step outputs: skip, mode, base, head, authors, count, files, pr_number, pr_url.
set -eu

ctx=_review/context
mkdir -p "$ctx" _review/out _review/scratch

skip() {
    printf 'skip=true\n' >> "$GITHUB_OUTPUT"
    printf 'Review skipped: %s\n' "$1" | tee -a "$GITHUB_STEP_SUMMARY"
    exit 0
}

head=$(git rev-parse HEAD)
if [ "$BRANCH" = "$DEFAULT_BRANCH" ]; then
    [ "$ACTOR" = "$OWNER" ] && skip "push to $DEFAULT_BRANCH by the repository owner"
    merged=$(gh api "repos/$GITHUB_REPOSITORY/commits/$head/pulls" \
        --jq '[.[] | select(.merged_at != null)] | length')
    [ "$merged" -gt 0 ] && skip "merge of a pull request whose branch was already reviewed"
    mode=direct-to-main
    if git cat-file -e "${BEFORE:-0}^{commit}" 2>/dev/null; then
        base=$BEFORE
    else
        base=$(git rev-parse --verify -q HEAD~1) || skip "no earlier commit to compare against"
    fi
else
    mode=branch
    base=$(git merge-base "origin/$DEFAULT_BRANCH" HEAD) \
        || skip "branch shares no history with $DEFAULT_BRANCH"
fi

count=$(git rev-list --count "$base..HEAD")
[ "$count" -gt 0 ] || skip "no commits beyond $DEFAULT_BRANCH"

git log --reverse --format='- %h %an <%ae>: %s' "$base..HEAD" > "$ctx/commits.md"
git diff --stat=120 "$base" HEAD > "$ctx/diffstat.txt"
git diff "$base" HEAD > "$ctx/diff.patch"
authors=$(git log --format='%an' "$base..HEAD" | sort -u | paste -sd ',' - | sed 's/,/, /g')
files=$(git diff --name-only "$base" HEAD | wc -l | tr -d ' ')

pr_number= pr_url=
if [ "$mode" = branch ]; then
    pr=$(gh pr list --head "$BRANCH" --state open --limit 1 --json number,url \
        --jq '.[0] // empty | "\(.number) \(.url)"')
    if [ -n "$pr" ]; then
        pr_number=${pr%% *}
        pr_url=${pr#* }
    fi
fi

cat > "$ctx/meta.md" <<EOF
# Review context

- Repository: $GITHUB_REPOSITORY
- Branch: $BRANCH ($mode)
- Base: $base
- Head: $head
- Pushed by: $ACTOR
- Commit authors: $authors
- Commits: $count; files changed: $files
- Open pull request: ${pr_url:-none}
- run.sh file mode: $(git ls-files -s run.sh | cut -d ' ' -f 1)

## Commits

$(cat "$ctx/commits.md")

## Diffstat

$(cat "$ctx/diffstat.txt")
EOF

{
    printf 'skip=false\nmode=%s\nbase=%s\nhead=%s\n' "$mode" "$base" "$head"
    printf 'authors=%s\ncount=%s\nfiles=%s\n' "$authors" "$count" "$files"
    printf 'pr_number=%s\npr_url=%s\n' "$pr_number" "$pr_url"
} >> "$GITHUB_OUTPUT"
