#!/usr/bin/env bash
# release.sh — stage, bump, commit, tag, push, wait for CI, upload to Steam
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
REPO=$(git remote get-url origin | sed 's|.*github.com[:/]||;s|\.git$||')

# ---------------------------------------------------------------------------
# Safety checks
# ---------------------------------------------------------------------------

if git rev-parse "$TAG" &>/dev/null; then
    echo "Error: tag $TAG already exists." >&2
    exit 1
fi

command -v gh &>/dev/null || { echo "Error: gh CLI not installed." >&2; exit 1; }
command -v steamcmd &>/dev/null || { echo "Error: steamcmd not installed." >&2; exit 1; }

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
echo "Pushed tag ${TAG}. CI will run in background for GitHub releases."
echo "  https://github.com/${REPO}/actions"
echo ""

# ---------------------------------------------------------------------------
# Build locally and upload to Steam (no GitHub dependency)
# ---------------------------------------------------------------------------

echo "Building and uploading to Steam locally..."
./tools/steam-upload.sh

echo ""
echo "=== Release ${TAG} complete ==="
echo "  Steam:  app 4627800, live on default branch"
echo "  GitHub: CI runs in background, release created when done"
