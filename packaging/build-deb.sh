#!/bin/bash
# Builds folio-gable_<version>_amd64.deb from this source tree.
# Works on Zorin OS 17 (Ubuntu 22.04) and Zorin OS 18 (Ubuntu 24.04).
#   sudo apt install qt6-base-dev libqt6opengl6-dev libgl1-mesa-dev libglu1-mesa-dev cmake ninja-build g++ dpkg-dev
#   ./packaging/build-deb.sh
set -euo pipefail
SRC="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="$(grep -oP 'project\(Gable VERSION \K[0-9.]+' "$SRC/CMakeLists.txt")"
BUILD="$SRC/build-release"
STAGE="$BUILD/stage"
OUT="${1:-$SRC}"

cmake -S "$SRC" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$BUILD"
"$BUILD/gable_tests" >/dev/null
rm -rf "$STAGE"
DESTDIR="$STAGE" cmake --install "$BUILD"
strip --strip-unneeded "$STAGE/usr/bin/folio-gable"
install -Dm644 "$SRC/README.md" "$STAGE/usr/share/doc/gable/README.md"

# Runtime dependencies from the binary's linked libraries
TMP="$(mktemp -d)"
mkdir -p "$TMP/debian"
printf 'Source: folio-gable\n\nPackage: folio-gable\nArchitecture: amd64\n' > "$TMP/debian/control"
DEPS="$(cd "$TMP" && dpkg-shlibdeps -O "$STAGE/usr/bin/folio-gable" 2>/dev/null | sed 's/^shlibs:Depends=//')"
rm -rf "$TMP"

mkdir -p "$STAGE/DEBIAN"
SIZE="$(du -sk "$STAGE/usr" | cut -f1)"
cat > "$STAGE/DEBIAN/control" <<EOF
Package: folio-gable
Version: $VERSION
Architecture: amd64
Maintainer: Ron Shillings <ron.shillings@gmail.com>
Installed-Size: $SIZE
Depends: $DEPS, qt6-qpa-plugins
Recommends: qt6-image-formats-plugins, qt6-wayland
Conflicts: gable
Replaces: gable
Section: graphics
Priority: optional
Description: Native 3D modeler with push/pull, inference and components
 Folio Gable is a fast, keyboard-driven 3D modeler for Linux in the style of
 classic push/pull modelers: sticky geometry with automatic faces,
 inference snapping with axis locks, groups and components, solid tools,
 follow me, section planes, sun shadows, scenes, styles, tags,
 materials with textures, dimensions and text, match photo, drawing sheets
 printed or saved as PDF, OBJ/STL/DAE/DXF import and export, 3DS, KMZ and
 IFC import, and glTF/GLB and 3MF export.
EOF
fakeroot dpkg-deb --build -Zxz "$STAGE" "$OUT/folio-gable_${VERSION}_amd64.deb" 2>/dev/null ||
    dpkg-deb --root-owner-group --build -Zxz "$STAGE" "$OUT/folio-gable_${VERSION}_amd64.deb"
echo "Built $OUT/folio-gable_${VERSION}_amd64.deb"
