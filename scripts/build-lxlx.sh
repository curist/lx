#!/bin/bash

set -eo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

LX=${LX:-}
if [[ -z "$LX" ]]; then
  if [[ -x "./out/lx" ]]; then
    LX="./out/lx"
  else
    LX=lx
  fi
fi
if [[ "$LX" == */* ]] && ! [ -x "$LX" ] && [ -x "$ROOT/$LX" ]; then
  LX="$ROOT/$LX"
fi
if ! command -v "$LX" >/dev/null 2>&1 && ! [ -x "$LX" ]; then
  echo skip building lxlx.h
  exit 0
fi

# Keep builtin hover docs bundled into the compiler bytecode.
"$LX" run scripts/gen-builtin-docs.lx > /dev/null

TARGET=./include/lx/lxlx.h
OBJ="$ROOT/out/lxlx-new.lxobj"

mkdir -p "$ROOT/out"

echo "#include <stdint.h>" > $TARGET
echo "const uint8_t lxlx_bytecode[] = {" >> $TARGET
# If $LX is the in-repo compiler (`out/lx`), use fast-path compile.
# Otherwise, use the driver pipeline to avoid bootstrapping mismatches.
if [[ "$LX" == *"/out/lx" || "$LX" == "out/lx" || "$LX" == "./out/lx" ]]; then
  if ! $LX compile main.lx > "$OBJ"; then
    $LX run scripts/build-lxlx-driver.lx > /dev/null
  fi
else
  $LX run scripts/build-lxlx-driver.lx > /dev/null
fi
xxd -i < "$OBJ" >> $TARGET
echo "};" >> $TARGET
