#!/usr/bin/env bash
set -euo pipefail

qt_prefix="$1"
if [[ -e "$qt_prefix/lib/libicui18n.so.73" ]]; then
    exit 0
fi

archive="$(mktemp)"
unpacked="$(mktemp -d)"
trap 'rm -f "$archive"; rm -rf "$unpacked"' EXIT
curl -fLsS --retry 3 \
    https://github.com/unicode-org/icu/releases/download/release-73-2/icu4c-73_2-Ubuntu22.04-x64.tgz \
    -o "$archive"
tar -xzf "$archive" -C "$unpacked"
icu_library="$(find "$unpacked" -type f -name 'libicui18n.so.73.2' -print -quit)"
test -n "$icu_library"
cp -a "$(dirname "$icu_library")"/libicu*.so* "$qt_prefix/lib/"
test -e "$qt_prefix/lib/libicui18n.so.73"
