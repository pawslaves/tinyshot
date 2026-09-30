#!/bin/sh
# Uninstall Tinyshot from the prefix scripts/install.sh installed it into.
#
# Usage: scripts/uninstall.sh [options]
#   --prefix DIR   install prefix (default: $PREFIX or ~/.local), the value
#                  scripts/install.sh was given
#   -h, --help     this help
#
# scripts/install.sh records the installed files, and the prefix they went
# into, in <prefix>/share/tinyshot/install-manifest.txt.  That record is the
# only thing this script trusts: it never removes a path outside the prefix,
# and never the prefix itself or its standard directories (bin, share,
# share/applications, share/icons/..., share/metainfo, share/doc).  The
# settings under ~/.config/tinyshot are left alone.
set -eu

usage() {
  sed -n '2,/^[^#]/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'
}

prefix=${PREFIX:-"$HOME/.local"}
while [ $# -gt 0 ]; do
  case $1 in
    --prefix) prefix=${2:?"--prefix needs a directory"}; shift 2 ;;
    --prefix=*) prefix=${1#*=}; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "uninstall.sh: unknown option: $1" >&2; usage >&2; exit 1 ;;
  esac
done

if [ ! -d "$prefix" ]; then
  echo "uninstall.sh: $prefix is not a directory - run scripts/install.sh first" >&2
  exit 1
fi
prefix=$(CDPATH= cd -- "$prefix" && pwd)
manifest=$prefix/share/tinyshot/install-manifest.txt
if [ ! -f "$manifest" ]; then
  echo "uninstall.sh: $manifest not found - run scripts/install.sh first" >&2
  exit 1
fi

# The record carries the prefix it was written for; refuse to act on a prefix
# the files were not installed into.
recorded_prefix=
while IFS= read -r line || [ -n "$line" ]; do
  case $line in
    prefix=*) recorded_prefix=${line#prefix=}; break ;;
  esac
done < "$manifest"
if [ "$recorded_prefix" != "$prefix" ]; then
  echo "uninstall.sh: $manifest was written for prefix '$recorded_prefix'," >&2
  echo "             not '$prefix'; nothing was removed" >&2
  exit 1
fi

# A directory below the prefix that stays even when the uninstall empties it:
# an emptied ~/.local/bin drops off PATH at the next login, and
# share/icons/... belongs to the icon theme.
is_standard_dir() {
  case ${1#"$prefix"/} in
    bin|share|share/applications|share/icons|share/icons/*|share/metainfo|share/doc) return 0 ;;
    *) return 1 ;;
  esac
}

# Remove the directories the install emptied, deepest first.  The walk stops
# before the prefix, so the prefix and every directory above it are safe.
prune_dirs() {
  {
    while IFS= read -r file || [ -n "$file" ]; do
      case $file in
        ''|'#'*|prefix=*) continue ;;
      esac
      case $file in "$prefix"/*) ;; *) continue ;; esac
      dir=${file%/*}
      while [ "$dir" != "$prefix" ] && [ "${dir#"$prefix"/}" != "$dir" ]; do
        printf '%s\n' "$dir"
        dir=${dir%/*}
      done
    done < "$manifest"
  } | sort -ru | while IFS= read -r dir || [ -n "$dir" ]; do
      [ -n "$dir" ] || continue
      is_standard_dir "$dir" && continue
      rmdir --ignore-fail-on-non-empty "$dir" 2>/dev/null || true
    done
}

# A cache file that a refresh tool just rewrote can be dropped when its
# directory holds nothing else.
drop_cache() {
  [ -f "$1" ] || return 0
  if [ -z "$(find "$(dirname -- "$1")" -mindepth 1 ! -name "$(basename -- "$1")" -print -quit 2>/dev/null)" ]; then
    rm -f -- "$1"
  fi
}

refresh_desktop_database() {
  [ -d "$1" ] || return 0
  if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$1" 2>/dev/null || true
  fi
  drop_cache "$1/mimeinfo.cache"
}

refresh_icon_cache() {
  [ -d "$1" ] || return 0
  if command -v gtk4-update-icon-cache >/dev/null 2>&1; then
    gtk4-update-icon-cache -q -t -f "$1" 2>/dev/null || true
  elif command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -q -t -f "$1" 2>/dev/null || true
  fi
  drop_cache "$1/icon-theme.cache"
}

# Remove the recorded files; a path that is not below the prefix is left alone.
removed=0
skipped=0
while IFS= read -r file || [ -n "$file" ]; do
  case $file in
    ''|'#'*|prefix=*) continue ;;
  esac
  case $file in
    "$prefix"/*) ;;
    *) echo "uninstall.sh: skipping $file (outside $prefix)" >&2
       skipped=$((skipped + 1))
       continue ;;
  esac
  [ -e "$file" ] || [ -L "$file" ] || continue
  rm -f -- "$file"
  removed=$((removed + 1))
done < "$manifest"
prune_dirs

# Refresh the caches of the directories the install wrote into, so the menu
# entry and the icons disappear immediately.
sed -n 's|\(.*\)/[^/]*\.desktop$|\1|p' "$manifest" | sort -u |
  while IFS= read -r dir || [ -n "$dir" ]; do
    case $dir in "$prefix"/*) refresh_desktop_database "$dir" ;; esac
  done
sed -n 's|\(.*/icons/hicolor\)/.*|\1|p' "$manifest" | sort -u |
  while IFS= read -r dir || [ -n "$dir" ]; do
    case $dir in "$prefix"/*) refresh_icon_cache "$dir" ;; esac
  done
prune_dirs

# The record itself lives in the prefix as well.
rm -f -- "$manifest"
rmdir --ignore-fail-on-non-empty "$prefix/share/tinyshot" 2>/dev/null || true

echo "removed $removed file(s) from $prefix"
if [ "$skipped" -ne 0 ]; then
  echo "uninstall.sh: skipped $skipped path(s) that were not below $prefix" >&2
fi
