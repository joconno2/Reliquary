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
echo "Pushed. Waiting for CI..."
echo "  https://github.com/${REPO}/actions"
echo ""

# ---------------------------------------------------------------------------
# Wait for CI to finish
# ---------------------------------------------------------------------------

# Wait for the CI run triggered by THIS tag to appear
echo "Waiting for CI run for ${TAG}..."
RUN_ID=""
for i in $(seq 1 30); do
    RUN_ID=$(gh run list -R "$REPO" --branch "${TAG}" --limit 1 --json databaseId --jq '.[0].databaseId' 2>/dev/null || echo "")
    [ -n "$RUN_ID" ] && break
    sleep 5
done

if [ -z "$RUN_ID" ]; then
    echo "Warning: couldn't find CI run for ${TAG} after 150s. Check manually."
    echo "After CI passes, run: ./tools/steam-upload.sh ${TAG}"
    exit 0
fi

echo "Watching run $RUN_ID..."
gh run watch "$RUN_ID" -R "$REPO" || true

# Check that the build jobs specifically passed
LINUX_WIN=$(gh run view "$RUN_ID" -R "$REPO" --json jobs --jq '.jobs[] | select(.name=="build-linux-windows") | .conclusion')
MACOS=$(gh run view "$RUN_ID" -R "$REPO" --json jobs --jq '.jobs[] | select(.name=="build-macos") | .conclusion')
RELEASE=$(gh run view "$RUN_ID" -R "$REPO" --json jobs --jq '.jobs[] | select(.name=="release") | .conclusion')

if [ "$LINUX_WIN" != "success" ] || [ "$MACOS" != "success" ]; then
    echo ""
    echo "Build failed! Linux/Win: $LINUX_WIN, macOS: $MACOS"
    echo "Check: https://github.com/${REPO}/actions/runs/${RUN_ID}"
    echo "Fix the issue, then: git tag -d ${TAG} && git push origin :refs/tags/${TAG}"
    exit 1
fi

echo ""
echo "Builds passed. (Linux/Win: $LINUX_WIN, macOS: $MACOS, Release: $RELEASE)"

# ---------------------------------------------------------------------------
# Upload to Steam
# ---------------------------------------------------------------------------

# Wait for GitHub Release assets to be available
echo ""
echo "Waiting for GitHub Release ${TAG} assets..."
for i in $(seq 1 24); do
    ASSET_COUNT=$(gh release view "${TAG}" -R "$REPO" --json assets --jq '.assets | length' 2>/dev/null || echo "0")
    if [ "$ASSET_COUNT" -ge 3 ] 2>/dev/null; then
        echo "Release has $ASSET_COUNT assets. Proceeding."
        break
    fi
    if [ "$i" -eq 24 ]; then
        echo "Warning: GitHub Release assets not ready after 2 minutes."
        echo "Run manually: ./tools/steam-upload.sh ${TAG}"
        exit 0
    fi
    sleep 5
done

echo ""
echo "Uploading to Steam..."
./tools/steam-upload.sh "${TAG}"

echo ""
echo "=== Release ${TAG} complete ==="
echo "  GitHub: https://github.com/${REPO}/releases/tag/${TAG}"
echo "  Steam:  app 4627800, live on default branch"
