#!/usr/bin/env bash
# release.sh — stage, bump, commit, tag, push, build locally, upload to Steam
#
# Idempotent: if the tag already exists at HEAD, skips git steps
# and retries the Steam upload. Safe to re-run after a partial failure.
#
# GitHub is backup only (no CI). Steam gets the local build.
#
# Usage:
#   ./release.sh           — auto-increments patch (0.1.0 → 0.1.1)
#   ./release.sh 0.2.0     — explicit version
set -euo pipefail

cd "$(dirname "$0")"

# ---------------------------------------------------------------------------
# Determine new version
# ---------------------------------------------------------------------------

CURRENT=$(grep -oP '(?<=project\(Reliquary VERSION )[0-9]+\.[0-9]+\.[0-9]+' CMakeLists.txt)

if [ $# -ge 1 ]; then
    NEW="$1"
else
    MAJOR=$(echo "$CURRENT" | cut -d. -f1)
    MINOR=$(echo "$CURRENT" | cut -d. -f2)
    PATCH=$(echo "$CURRENT" | cut -d. -f3)
    NEW="${MAJOR}.${MINOR}.$((PATCH + 1))"
fi

TAG="v${NEW}"

# ---------------------------------------------------------------------------
# Safety checks
# ---------------------------------------------------------------------------

command -v steamcmd &>/dev/null || { echo "Error: steamcmd not installed." >&2; exit 1; }

# ---------------------------------------------------------------------------
# Idempotent git: skip if tag exists at HEAD
# ---------------------------------------------------------------------------

if git rev-parse "$TAG" &>/dev/null; then
    TAG_SHA=$(git rev-parse "$TAG^{}")
    HEAD_SHA=$(git rev-parse HEAD)
    if [ "$TAG_SHA" = "$HEAD_SHA" ]; then
        echo "=== Reliquary Release (retry) ==="
        echo "Tag $TAG already exists at HEAD. Skipping git steps."
        echo "Retrying Steam upload..."
        echo ""
        ./tools/steam-upload.sh
        echo ""
        echo "=== Release ${TAG} complete ==="
        echo "  Steam: app 4627800, uploaded (set live manually on Steamworks)"
        exit 0
    else
        echo "Error: tag $TAG exists but does not point to HEAD." >&2
        echo "  tag: $TAG_SHA" >&2
        echo "  HEAD: $HEAD_SHA" >&2
        echo "If you want a new release, increment the version." >&2
        exit 1
    fi
fi

# ---------------------------------------------------------------------------
# Stage everything
# ---------------------------------------------------------------------------

git add -A

echo "=== Reliquary Release ==="
echo ""
git diff --cached --stat
echo ""
echo "Current: $CURRENT  ->  New: $NEW  ($TAG)"
echo ""
read -rp "Proceed? [y/N] " CONFIRM
[[ "$CONFIRM" =~ ^[Yy]$ ]] || {
    git reset HEAD -- . 2>/dev/null || true
    echo "Aborted."
    exit 0
}

# ---------------------------------------------------------------------------
# Bump, commit, tag, push
# ---------------------------------------------------------------------------

sed -i "s/project(Reliquary VERSION ${CURRENT}/project(Reliquary VERSION ${NEW}/" CMakeLists.txt
git add CMakeLists.txt
git commit -m "Release ${TAG}"
git tag "${TAG}"
git push origin main
git push origin "${TAG}"

echo ""
echo "Pushed ${TAG} to GitHub (backup)."
echo ""

# ---------------------------------------------------------------------------
# Build locally and upload to Steam
# ---------------------------------------------------------------------------

echo "Building and uploading to Steam..."
./tools/steam-upload.sh

echo ""
echo "=== Release ${TAG} complete ==="
echo "  Steam: app 4627800, uploaded (set live manually on Steamworks)"
echo "  GitHub: pushed (backup only)"
