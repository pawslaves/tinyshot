#!/bin/sh
# Build and install Tinyshot into a user-local prefix (no sudo).
#
# Usage: scripts/install.sh [options]
#   --prefix DIR        install prefix (default: $PREFIX or ~/.local)
#   --build-dir DIR     build directory (default: <repo>/build)
#   -h, --help          this help
#
# The installer only touches the prefix; the desktop database and the icon
# cache are refreshed when the tools for it are installed.  It records the
# installed files, and the prefix itself, in
# <prefix>/share/tinyshot/install-manifest.txt, the record scripts/uninstall.sh
# removes the installation from.
set -eu

usage() {
  sed -n '2,/^[^#]/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'
}

prefix=${PREFIX:-"$HOME/.local"}
build_dir=
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

while [ $# -gt 0 ]; do
  case $1 in
    --prefix) prefix=${2:?"--prefix needs a directory"}; shift 2 ;;
    --prefix=*) prefix=${1#*=}; shift ;;
    --build-dir) build_dir=${2:?"--build-dir needs a directory"}; shift 2 ;;
    --build-dir=*) build_dir=${1#*=}; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "install.sh: unknown option: $1" >&2; usage >&2; exit 1 ;;
  esac
done

if [ -z "$build_dir" ]; then
  build_dir=$repo_dir/build
fi

# The uninstall manifest records absolute paths, so resolve the prefix here
# (mkdir also creates a prefix that does not exist yet, as cmake --install
# would do).  Both scripts resolve it the same way, so they agree on the paths.
mkdir -p -- "$prefix"
prefix=$(CDPATH= cd -- "$prefix" && pwd)

generator=
if command -v ninja >/dev/null 2>&1; then
  generator='-G Ninja'
fi

# shellcheck disable=SC2086  # $generator is either "-G Ninja" or empty
set -- -S "$repo_dir" -B "$build_dir" $generator \
       -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix"

cmake "$@"
cmake --build "$build_dir"
cmake --install "$build_dir"

# Point the installed desktop entry at this prefix's binary: the menu entry
# must start it even when $prefix/bin is not on PATH.  (The .deb keeps the
# plain "Exec=tinyshot"; there the binary is in /usr/bin.)
desktop=$prefix/share/applications/io.github.pawslaves.Tinyshot.desktop
if [ -f "$desktop" ]; then
  desktop_exec=$(printf '%s\n' "$prefix/bin/tinyshot" | sed 's/[\\&|]/\\&/g')
  sed -i "s|^Exec=.*|Exec=\"$desktop_exec\"|" "$desktop"
fi

# Record what went where.  <build-dir>/install_manifest.txt is rewritten by
# every install in that tree, CPack's staging install (`--target package`
# included), so the uninstaller gets its own copy, inside the prefix.
manifest_dir=$prefix/share/tinyshot
manifest=$manifest_dir/install-manifest.txt
mkdir -p -- "$manifest_dir"
{
  echo "# Files scripts/install.sh installed, and where; written by the installer,"
  echo "# read by scripts/uninstall.sh.  Do not edit."
  echo "prefix=$prefix"
  cat -- "$build_dir/install_manifest.txt"
  echo  # cmake's manifest has no final newline; terminate the last entry
} > "$manifest"

if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database "$prefix/share/applications" 2>/dev/null || true
fi
if command -v gtk4-update-icon-cache >/dev/null 2>&1; then
  gtk4-update-icon-cache -q -t -f "$prefix/share/icons/hicolor" 2>/dev/null || true
elif command -v gtk-update-icon-cache >/dev/null 2>&1; then
  gtk-update-icon-cache -q -t -f "$prefix/share/icons/hicolor" 2>/dev/null || true
fi

echo "installed tinyshot to $prefix (binary: $prefix/bin/tinyshot)"
