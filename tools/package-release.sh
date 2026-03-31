#!/bin/bash
# Package Reliquary for distribution
# Usage: ./tools/package-release.sh [version]
# Outputs: reliquary-linux-x64-vX.Y.Z.tar.gz
#          reliquary-windows-x64-vX.Y.Z.zip (if Windows build exists)

set -e
cd "$(dirname "$0")/.."

VERSION="${1:-0.1.0-alpha}"
echo "Packaging Reliquary $VERSION"

# --- Linux ---
echo "=== Linux build ==="
cmake -B build-release -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE="-O2 -DNDEBUG" 2>&1 | tail -1
cmake --build build-release -j$(nproc) 2>&1 | tail -1
strip build-release/reliquary

LINUX_DIR="reliquary-linux-x64-$VERSION"
rm -rf "$LINUX_DIR"
mkdir -p "$LINUX_DIR"
cp build-release/reliquary "$LINUX_DIR/"
cp -r build-release/assets "$LINUX_DIR/"
cp -r data "$LINUX_DIR/"
mkdir -p "$LINUX_DIR/save"

# Create run script
cat > "$LINUX_DIR/run.sh" << 'RUNEOF'
#!/bin/bash
cd "$(dirname "$0")"
./reliquary
RUNEOF
chmod +x "$LINUX_DIR/run.sh"

tar czf "${LINUX_DIR}.tar.gz" "$LINUX_DIR"
rm -rf "$LINUX_DIR"
echo "Created ${LINUX_DIR}.tar.gz ($(du -h "${LINUX_DIR}.tar.gz" | cut -f1))"

# --- Windows (if MinGW SDL2 libs are available) ---
if [ -f /usr/x86_64-w64-mingw32/lib/libSDL2.dll.a ]; then
    echo "=== Windows cross-compile ==="
    cmake -B build-win64 \
        -DCMAKE_TOOLCHAIN_FILE=tools/mingw-toolchain.cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_FLAGS_RELEASE="-O2 -DNDEBUG" 2>&1 | tail -1
    cmake --build build-win64 -j$(nproc) 2>&1 | tail -1
    x86_64-w64-mingw32-strip build-win64/reliquary.exe

    WIN_DIR="reliquary-windows-x64-$VERSION"
    rm -rf "$WIN_DIR"
    mkdir -p "$WIN_DIR"
    cp build-win64/reliquary.exe "$WIN_DIR/"
    cp -r build-win64/assets "$WIN_DIR/"
    cp -r data "$WIN_DIR/"
    mkdir -p "$WIN_DIR/save"

    # Bundle SDL2 DLLs
    for dll in SDL2.dll SDL2_image.dll SDL2_mixer.dll SDL2_ttf.dll; do
        found=$(find /usr/x86_64-w64-mingw32 -name "$dll" 2>/dev/null | head -1)
        if [ -n "$found" ]; then
            cp "$found" "$WIN_DIR/"
        fi
    done
    # Also bundle common deps
    for dll in libpng16-16.dll libjpeg-8.dll zlib1.dll libfreetype-6.dll; do
        found=$(find /usr/x86_64-w64-mingw32 -name "$dll" 2>/dev/null | head -1)
        if [ -n "$found" ]; then
            cp "$found" "$WIN_DIR/"
        fi
    done

    zip -rq "${WIN_DIR}.zip" "$WIN_DIR"
    rm -rf "$WIN_DIR"
    echo "Created ${WIN_DIR}.zip ($(du -h "${WIN_DIR}.zip" | cut -f1))"
else
    echo "=== Windows build skipped (install mingw-w64-sdl2 packages first) ==="
    echo "  paru -S mingw-w64-sdl2 mingw-w64-sdl2_image mingw-w64-sdl2_mixer mingw-w64-sdl2_ttf"
fi

echo "Done."
