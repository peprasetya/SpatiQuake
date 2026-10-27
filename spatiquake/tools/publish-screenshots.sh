#!/usr/bin/env bash
# Put a CI job's screenshots on the ci-screenshots branch, in a folder named after the job, so they
# can be looked at with git (artifacts are not reachable from everywhere).
#
#   spatiquake/tools/publish-screenshots.sh DIR JOB
#
# The branch holds only the latest pictures of each job; its history is a log of runs.
set -euo pipefail
DIR="$(realpath "$1")"
JOB="$2"
BRANCH=ci-screenshots
WORK="$(mktemp -d)"
git config --global user.name "SpatiQuake CI"
git config --global user.email "ci@users.noreply.github.com"
if git fetch -q origin "$BRANCH" 2>/dev/null; then
    git worktree add -q "$WORK" "origin/$BRANCH"
    git -C "$WORK" checkout -q -B "$BRANCH"
else
    git worktree add -q --detach "$WORK"
    git -C "$WORK" checkout -q --orphan "$BRANCH"
    git -C "$WORK" rm -rq --cached . >/dev/null 2>&1 || true
    find "$WORK" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {} +
fi
rm -rf "${WORK:?}/$JOB"
mkdir -p "$WORK/$JOB"
cp -a "$DIR"/. "$WORK/$JOB/"
git -C "$WORK" add -A "$JOB"
git -C "$WORK" commit -q -m "$JOB screenshots for ${GITHUB_SHA:-local}" || exit 0
for attempt in 1 2 3; do
    git -C "$WORK" push -q origin "$BRANCH" && exit 0
    git -C "$WORK" pull -q --rebase origin "$BRANCH" || true
done
exit 1
