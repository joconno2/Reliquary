#!/usr/bin/env bash
# release.sh — stage, bump version, commit, tag, push → triggers CI build + release
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

if git rev-parse "$TAG" &>/dev/null; then
    echo "Error: tag $TAG already exists." >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Stage everything
# ---------------------------------------------------------------------------

git add -A

echo "Changes to be committed:"
echo ""
git diff --cached --stat
echo ""

echo "Current version : $CURRENT"
echo "New version     : $NEW  ($TAG)"
echo ""
read -rp "Proceed? [y/N] " CONFIRM
[[ "$CONFIRM" =~ ^[Yy]$ ]] || {
    git reset HEAD -- . 2>/dev/null || true
    echo "Aborted."
    exit 0
}

# ---------------------------------------------------------------------------
# Bump CMakeLists.txt
# ---------------------------------------------------------------------------

sed -i "s/project(Reliquary VERSION ${CURRENT}/project(Reliquary VERSION ${NEW}/" CMakeLists.txt
git add CMakeLists.txt

# ---------------------------------------------------------------------------
# Commit, tag, push
# ---------------------------------------------------------------------------

git commit -m "Release ${TAG}"
git tag "${TAG}"
git push origin main
git push origin "${TAG}"

echo ""
REMOTE_URL=$(git remote get-url origin | sed 's|.*github.com[:/]||;s|\.git$||')
echo "Done. Track the build at:"
echo "  https://github.com/${REMOTE_URL}/actions"
echo ""
echo "Release page (ready in ~5 min):"
echo "  https://github.com/${REMOTE_URL}/releases/tag/${TAG}"
