#!/usr/bin/env bash
# What the runtime program builder (T3_BUILDER=1) embeds, into third_party/:
# quickjs-ng (the JS engine) and the three.js npm package whose node renderer
# writes the programs. Both archives are checked against their SHA-256.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
QJS_VER=0.17.0
QJS_SHA=559bc4c420475e55c7ab4510adbc562f55d7524d75e8e89d79ce4bb02f5687d9
THREE_VER=0.186.1
THREE_SHA=8cd068708ea44f2c73c944b1cead2ba2f0d5c15c8fc194e5700f4e4f4a033fe7
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
get() {   # url sha dest
  [ -d "$3" ] && { echo "already there: ${3#$ROOT/}"; return; }
  curl -fsSL "$1" -o "$TMP/a.tgz"
  echo "$2  $TMP/a.tgz" | sha256sum -c - >/dev/null
  mkdir -p "$TMP/x" "$3"
  tar -xzf "$TMP/a.tgz" -C "$TMP/x"
  cp -r "$TMP"/x/*/. "$3"/
  rm -rf "$TMP/x"
  echo "fetched ${3#$ROOT/}"
}
get "https://github.com/quickjs-ng/quickjs/archive/refs/tags/v$QJS_VER.tar.gz" $QJS_SHA "$ROOT/third_party/quickjs"
get "https://registry.npmjs.org/three/-/three-$THREE_VER.tgz" $THREE_SHA "$ROOT/third_party/three-$THREE_VER"
