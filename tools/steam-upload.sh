#!/usr/bin/env bash
# Upload latest release builds to Steam
# Usage: ./tools/steam-upload.sh [TAG]
#   Defaults to latest tag if not specified.
set -euo pipefail
cd "$(dirname "$0")/.."

APP_ID=4627800
DEPOT_LINUX=4627801    # "Reliquary Content" — shared content + Linux binary
DEPOT_WINDOWS=4627802  # "Windows"
DEPOT_MACOS=4627803    # "macOS"

TAG="${1:-$(git describe --tags --abbrev=0)}"
echo "=== Steam Upload: Reliquary ${TAG} ==="
echo "App ID: ${APP_ID}"
echo ""

# Download release artifacts from GitHub
mkdir -p /tmp/reliquary-steam
cd /tmp/reliquary-steam
rm -rf linux windows macos *.tar.gz *.zip

echo "Downloading builds from GitHub Release ${TAG}..."
gh release download "${TAG}" -R joconno2/Reliquary -p "*.tar.gz" -p "*.zip" --clobber

echo "Extracting..."
mkdir -p linux windows macos
tar xzf reliquary-linux-*.tar.gz -C linux --strip-components=1
unzip -qo reliquary-windows-*.zip -d windows_tmp && cp -r windows_tmp/reliquary-windows-*/* windows/ && rm -rf windows_tmp
tar xzf reliquary-macos-*.tar.gz -C macos --strip-components=1

echo ""
echo "Contents:"
echo "  Linux:   $(ls linux/ | wc -l) files"
echo "  Windows: $(ls windows/ | wc -l) files"
echo "  macOS:   $(ls macos/ | wc -l) files"
echo ""

# Create VDF build scripts
cat > app_build.vdf << VDFEOF
"AppBuild"
{
    "AppID" "${APP_ID}"
    "Desc" "Reliquary ${TAG}"
    "BuildOutput" "/tmp/reliquary-steam/output/"
    "ContentRoot" "/tmp/reliquary-steam/"
    "SetLive" "default"
    "Depots"
    {
        "${DEPOT_LINUX}"
        {
            "FileMapping"
            {
                "LocalPath" "linux/*"
                "DepotPath" "."
                "recursive" "1"
            }
        }
        "${DEPOT_WINDOWS}"
        {
            "FileMapping"
            {
                "LocalPath" "windows/*"
                "DepotPath" "."
                "recursive" "1"
            }
        }
        "${DEPOT_MACOS}"
        {
            "FileMapping"
            {
                "LocalPath" "macos/*"
                "DepotPath" "."
                "recursive" "1"
            }
        }
    }
}
VDFEOF

mkdir -p output

echo "Uploading to Steam..."
steamcmd +login blademaster313 +run_app_build /tmp/reliquary-steam/app_build.vdf +quit

echo ""
echo "Done. Build live on default branch."
