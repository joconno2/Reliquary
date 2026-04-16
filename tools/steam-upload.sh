#!/usr/bin/env bash
# Build locally and upload to Steam
# Usage: ./tools/steam-upload.sh
#
# Logs to ~/Reliquary/logs/steam-upload-YYYY-MM-DD-HHMMSS.log
# On failure, prints the log path and last 20 lines.
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT"

APP_ID=4627800
DEPOT_LINUX=4627801
DEPOT_WINDOWS=4627802
TAG="$(git describe --tags --abbrev=0 2>/dev/null || echo 'dev')"

# ── Logging ──
mkdir -p "$PROJECT_ROOT/logs"
LOGFILE="$PROJECT_ROOT/logs/steam-upload-$(date +%F-%H%M%S).log"

log() { echo "[$(date +%H:%M:%S)] $*" | tee -a "$LOGFILE"; }
fail() { log "FAILED: $*"; echo ""; echo "Log: $LOGFILE"; tail -20 "$LOGFILE"; exit 1; }

log "=== Steam Upload: Reliquary ${TAG} ==="
log "App ID: ${APP_ID}"
log "Project root: ${PROJECT_ROOT}"

STAGING=/tmp/reliquary-steam
rm -rf "$STAGING"
mkdir -p "$STAGING"/{linux,windows,output}

# ── Linux: build locally ──
log "Building Linux..."
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -G Ninja >> "$LOGFILE" 2>&1 \
    || fail "Linux cmake configure"
cmake --build build-release --parallel $(nproc) >> "$LOGFILE" 2>&1 \
    || fail "Linux build"
strip build-release/reliquary
log "Linux build OK"

log "Packaging Linux..."
cp build-release/reliquary "$STAGING/linux/"
cp -r assets "$STAGING/linux/"
cp -r data "$STAGING/linux/"
mkdir -p "$STAGING/linux/save"
printf '#!/bin/bash\ncd "$(dirname "$0")"\n./reliquary\n' > "$STAGING/linux/run.sh"
chmod +x "$STAGING/linux/run.sh"
log "Linux packaged: $(ls "$STAGING/linux/" | wc -l) top-level entries"

# ── Windows: cross-compile locally ──
log "Building Windows..."
SDL2_DIR="${PROJECT_ROOT}/deps/windows"
if [ ! -d "$SDL2_DIR" ]; then
    log "Fetching Windows SDL2 packages..."
    mkdir -p "$SDL2_DIR"
    (
        cd "$SDL2_DIR"
        wget -q "https://github.com/libsdl-org/SDL/releases/download/release-2.32.4/SDL2-devel-2.32.4-mingw.tar.gz"
        wget -q "https://github.com/libsdl-org/SDL_image/releases/download/release-2.8.8/SDL2_image-devel-2.8.8-mingw.tar.gz"
        wget -q "https://github.com/libsdl-org/SDL_ttf/releases/download/release-2.24.0/SDL2_ttf-devel-2.24.0-mingw.tar.gz"
        wget -q "https://github.com/libsdl-org/SDL_mixer/releases/download/release-2.8.1/SDL2_mixer-devel-2.8.1-mingw.tar.gz"
        for f in *.tar.gz; do tar xf "$f"; done
    ) >> "$LOGFILE" 2>&1 || fail "SDL2 download/extract"
    log "SDL2 packages fetched"
fi

SDL2_PREFIX="${SDL2_DIR}/SDL2-2.32.4/x86_64-w64-mingw32"
SDL2_IMAGE_PREFIX="${SDL2_DIR}/SDL2_image-2.8.8/x86_64-w64-mingw32"
SDL2_TTF_PREFIX="${SDL2_DIR}/SDL2_ttf-2.24.0/x86_64-w64-mingw32"
SDL2_MIXER_PREFIX="${SDL2_DIR}/SDL2_mixer-2.8.1/x86_64-w64-mingw32"
ALL_INCLUDES="${SDL2_PREFIX}/include;${SDL2_PREFIX}/include/SDL2;${SDL2_IMAGE_PREFIX}/include;${SDL2_IMAGE_PREFIX}/include/SDL2;${SDL2_TTF_PREFIX}/include;${SDL2_TTF_PREFIX}/include/SDL2;${SDL2_MIXER_PREFIX}/include;${SDL2_MIXER_PREFIX}/include/SDL2"

cmake -S . -B build-windows -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=tools/mingw-toolchain.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DSDL2_INCLUDE_DIRS="${ALL_INCLUDES}" \
    -DSDL2_LIBRARIES="-L${SDL2_PREFIX}/lib -lmingw32 -lSDL2main -lSDL2" \
    -DSDL2_IMAGE_INCLUDE_DIRS="" \
    -DSDL2_IMAGE_LIBRARIES="-L${SDL2_IMAGE_PREFIX}/lib -lSDL2_image" \
    -DSDL2_TTF_INCLUDE_DIRS="" \
    -DSDL2_TTF_LIBRARIES="-L${SDL2_TTF_PREFIX}/lib -lSDL2_ttf" \
    -DSDL2_MIXER_INCLUDE_DIRS="" \
    -DSDL2_MIXER_LIBRARIES="-L${SDL2_MIXER_PREFIX}/lib -lSDL2_mixer" \
    >> "$LOGFILE" 2>&1 || fail "Windows cmake configure"
cmake --build build-windows --parallel $(nproc) >> "$LOGFILE" 2>&1 \
    || fail "Windows build"
x86_64-w64-mingw32-strip build-windows/reliquary.exe
log "Windows build OK"

log "Packaging Windows..."
cp build-windows/reliquary.exe "$STAGING/windows/"
cp -r assets "$STAGING/windows/"
cp -r data "$STAGING/windows/"
mkdir -p "$STAGING/windows/save"

# SDL2 DLLs
for prefix in SDL2-2.32.4 SDL2_image-2.8.8 SDL2_ttf-2.24.0 SDL2_mixer-2.8.1; do
    find "${SDL2_DIR}/${prefix}/x86_64-w64-mingw32/bin" -name "*.dll" -exec cp {} "$STAGING/windows/" \; 2>/dev/null || true
done

# MinGW runtime DLLs
MINGW_BIN=$(dirname $(x86_64-w64-mingw32-g++ -print-file-name=libstdc++-6.dll) 2>/dev/null || echo "")
for dll in libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll; do
    if [ -n "$MINGW_BIN" ] && [ -f "$MINGW_BIN/$dll" ]; then
        cp "$MINGW_BIN/$dll" "$STAGING/windows/"
    else
        found=$(find /usr -name "$dll" 2>/dev/null | head -1)
        [ -n "$found" ] && cp "$found" "$STAGING/windows/"
    fi
done

# Log DLL manifest for debugging
log "Windows DLLs:"
ls "$STAGING/windows/"*.dll >> "$LOGFILE" 2>&1
log "exe imports:"
x86_64-w64-mingw32-objdump -p "$STAGING/windows/reliquary.exe" 2>/dev/null | grep "DLL Name" >> "$LOGFILE" || true
log "Windows packaged: $(ls "$STAGING/windows/" | wc -l) top-level entries"

# ── Upload to Steam ──
cat > "$STAGING/app_build.vdf" << VDFEOF
"AppBuild"
{
    "AppID" "${APP_ID}"
    "Desc" "Reliquary ${TAG}"
    "BuildOutput" "${STAGING}/output/"
    "ContentRoot" "${STAGING}/"
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
    }
}
VDFEOF

log "Uploading to Steam..."
steamcmd +login blademaster313 +run_app_build "$STAGING/app_build.vdf" +quit >> "$LOGFILE" 2>&1 \
    || fail "Steam upload"

log ""
log "=== Done. Build live on default branch. ==="
log "Linux: $(ls "$STAGING/linux/" | wc -l) entries"
log "Windows: $(ls "$STAGING/windows/" | wc -l) entries"
log "Log: $LOGFILE"
