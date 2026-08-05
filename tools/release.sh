#!/bin/sh
# release.sh -- verify and tag a CCCC release (#883).
#
# Does NOT push anything. Verification, tagging, and publishing are kept as
# separate deliberate steps: a bad tag is easy to fix locally, a bad push
# to a shared remote is not (see CLAUDE.md's Git Safety Protocol).
#
# Usage: sh tools/release.sh <version>   e.g. sh tools/release.sh 0.1.0
set -e

VERSION=${1:?"usage: release.sh <version>  e.g. release.sh 0.1.0"}
TAG="v$VERSION"

# 1. Clean tree -- an uncommitted change silently missing from the tagged
#    commit is exactly the failure mode this check exists to catch.
if [ -n "$(git status --porcelain)" ]; then
    echo "release: working tree is not clean; commit or stash first" >&2
    git status --short >&2
    exit 1
fi

# 2. Version constant in source must match the requested tag.
SRC_VERSION=$(grep -o 'CCCC_RELEASE_VERSION "[^"]*"' src/internal.h | sed 's/.*"\(.*\)"/\1/')
if [ "$SRC_VERSION" != "$VERSION" ]; then
    echo "release: src/internal.h has CCCC_RELEASE_VERSION \"$SRC_VERSION\", requested $VERSION" >&2
    echo "release: update the #define and commit before tagging" >&2
    exit 1
fi

# 3. CHANGELOG.md must have a matching section.
if ! grep -q "^## \[$VERSION\]" CHANGELOG.md; then
    echo "release: CHANGELOG.md has no '## [$VERSION]' section" >&2
    exit 1
fi

# 4. Full test suite must pass before anything is tagged.
./cccc --build build.c --build-target=test

# 5. Extract the CHANGELOG section for this version as the tag message.
NOTES=$(mktemp)
trap 'rm -f "$NOTES"' EXIT
awk -v ver="$VERSION" '
    /^## \[/ { if (found) exit; if ($0 ~ "\\[" ver "\\]") { found=1; next } }
    found { print }
' CHANGELOG.md > "$NOTES"

git tag -a "$TAG" -F "$NOTES"

echo "release: tagged $TAG locally. Nothing has been pushed."
echo "release: to publish, run:"
echo "  git push origin trunk"
echo "  git push origin $TAG"
echo "  git push github main"
echo "  git push github $TAG"
