#!/usr/bin/env bash
# Dawn's emdawnwebgpu port (webgpu.h for Emscripten) into
# third_party/emdawnwebgpu, for WGPU=1 wasm/build.sh. The release is the one
# wasmcart's WebGPU tier freezes its imports to; the archive is checked
# against its SHA-256.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REL=v20261002.154047
SHA=6b14532b85c7f4d51154b6eeb6bd3fcf7735ad5c7cfd698d83985dd21a1c15c2
URL="https://github.com/google/dawn/releases/download/$REL/emdawnwebgpu_pkg-$REL.zip"
DEST="$ROOT/third_party/emdawnwebgpu"
[ -f "$DEST/emdawnwebgpu.port.py" ] && { echo "already there: ${DEST#$ROOT/}"; exit 0; }
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
curl -fsSL "$URL" -o "$TMP/pkg.zip"
echo "$SHA  $TMP/pkg.zip" | sha256sum -c - >/dev/null
unzip -q "$TMP/pkg.zip" -d "$TMP/x"
mkdir -p "$DEST"
cp -r "$TMP"/x/*/. "$DEST"/
echo "fetched emdawnwebgpu $REL into ${DEST#$ROOT/}"
