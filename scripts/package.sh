#!/usr/bin/env bash
# Збирає пакети GameX для встановлення без сирців: .deb і .tar.gz.
# GitHub Actions навмисно не викликає цей скрипт; артефакти кладуть у
# GitHub Release вручну (див. packaging/GITHUB_RELEASE_v0.1.md).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${GAMEX_BUILD_DIR:-$ROOT/build-package}"
DIST="$ROOT/dist"

CMAKE_ARGS=(-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DGAMEX_BUILD_TESTS=ON)
if [[ -n "${GAMEX_QT_ROOT:-}" ]]; then
  CMAKE_ARGS+=(-DGAMEX_QT_ROOT="$GAMEX_QT_ROOT")
  echo "GAMEX_QT_ROOT: $GAMEX_QT_ROOT"
elif [[ -n "${CMAKE_PREFIX_PATH:-}" ]]; then
  CMAKE_ARGS+=(-DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH")
else
  # Офіційний інсталер: ~/Qt/6.10.2/gcc_64, ~/Qt/6.12.0/gcc_64, …
  qt_prefix="$(ls -1d "$HOME"/Qt/[0-9]*/gcc_64 2>/dev/null | sort -V | tail -1 || true)"
  if [[ -n "$qt_prefix" ]]; then
    CMAKE_ARGS+=(-DGAMEX_QT_ROOT="$qt_prefix")
    echo "Qt prefix: $qt_prefix"
  fi
fi

cmake -S "$ROOT" -B "$BUILD" -G Ninja "${CMAKE_ARGS[@]}"
cmake --build "$BUILD" -j"$(nproc)" --target gamex gamex_core_tests
"$BUILD/gamex_core_tests"

rm -rf "$DIST"
mkdir -p "$DIST/AppDir"
cmake --install "$BUILD" --prefix "$DIST/AppDir/usr"

# CPack пише .deb/.tar.gz поруч із білдом.
cmake --build "$BUILD" --target package
find "$BUILD" -maxdepth 1 \( -name 'gamex*.deb' -o -name 'gamex*.tar.gz' \) \
  -exec cp -a {} "$DIST/" \;

echo "Готово. Каталог артефактів: $DIST"
ls -la "$DIST"
