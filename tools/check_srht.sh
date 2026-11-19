#!/bin/sh
# check_srht.sh -- fail unless sr.ht built and tested a commit on linux-amd64.
#
# Usage: sh tools/check_srht.sh [commit]     (default HEAD)
#
# Looks for the .builds/linux-amd64.yml job that ran on trunk for the commit
# (or, when the commit is not trunk's tip, for trunk's tip if the trees are
# identical, as with a merge) and requires its `test` task to have passed.
set -e

die() {
    echo "check_srht: $*" >&2
    exit 1
}

for tool in hut jq; do
    command -v "$tool" >/dev/null 2>&1 || die "$tool not found (set CCCC_RELEASE_SKIP_SRHT=1 to skip)"
done

COMMIT=$(git rev-parse "${1:-HEAD}")
TRUNK=$(git rev-parse trunk)
if [ "$COMMIT" != "$TRUNK" ]; then
    git diff --quiet trunk "$COMMIT" ||
        die "$COMMIT is not trunk's tip and differs from it; sr.ht only builds trunk (hotfix? set CCCC_RELEASE_SKIP_SRHT=1)"
    COMMIT=$TRUNK
fi

CURSOR=
PAGES=0
while [ "$PAGES" -lt 8 ]; do
    ARGS=
    [ -n "$CURSOR" ] && ARGS="(cursor: \\\"$CURSOR\\\")"
    PAGE=$(echo "query { jobs$ARGS { cursor results { id status note tags tasks { name status } } } }" |
        hut graphql builds) || die "hut could not query builds.sr.ht"
    JOB=$(printf '%s\n' "$PAGE" | jq -c --arg sha "$COMMIT" '
        [.jobs.results[]
         | select((.tags | index("linux-amd64.yml")) and (.tags | index("trunk")))
         | select(.note | contains("/commit/" + $sha))]
        | first // empty')
    [ -n "$JOB" ] && break
    CURSOR=$(printf '%s\n' "$PAGE" | jq -r '.jobs.cursor // empty')
    [ -n "$CURSOR" ] || break
    PAGES=$((PAGES + 1))
done

[ -n "$JOB" ] || die "no sr.ht linux-amd64 job found for $COMMIT; push trunk to mirror and wait for it"

ID=$(printf '%s\n' "$JOB" | jq -r .id)
STATUS=$(printf '%s\n' "$JOB" | jq -r .status)
TEST=$(printf '%s\n' "$JOB" | jq -r '[.tasks[] | select(.name == "test") | .status] | first // "MISSING"')
URL="https://builds.sr.ht/~takeiteasy/job/$ID"

case "$STATUS" in
    SUCCESS) ;;
    PENDING | QUEUED | RUNNING) die "sr.ht job $ID is still $STATUS: $URL" ;;
    *) die "sr.ht job $ID is $STATUS: $URL" ;;
esac
[ "$TEST" = SUCCESS ] || die "sr.ht job $ID passed but its test task is $TEST: $URL"

echo "check_srht: job $ID passed for $COMMIT"
