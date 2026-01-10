#!/bin/bash

set -eo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

LX=${LX:-lx}
if ! command -v "$LX" >/dev/null 2>&1 && ! [ -x "$LX" ]; then
  echo skip building lxlx.h
  exit 0
fi

TARGET=./include/lx/lxlx.h
OBJ="$ROOT/out/lxlx-new.lxobj"

mkdir -p "$ROOT/out"

echo "#include <stdint.h>" > $TARGET
echo "const uint8_t lxlx_bytecode[] = {" >> $TARGET
# If $LX is the in-repo compiler (`out/lx`), use fast-path compile.
# Otherwise, use the driver pipeline to avoid bootstrapping mismatches.
if [[ "$LX" == *"/out/lx" || "$LX" == "out/lx" || "$LX" == "./out/lx" ]]; then
  if ! $LX compile lx/main.lx > "$OBJ"; then
    $LX run lx/scripts/bootstrap-codegen.lx lx/main.lx
    cp /tmp/main.lxobj "$OBJ"
  fi
else
  $LX run lx/scripts/bootstrap-codegen.lx lx/main.lx
  cp /tmp/main.lxobj "$OBJ"
fi
xxd -i < "$OBJ" >> $TARGET
echo "};" >> $TARGET
