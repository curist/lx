#!/bin/bash

set -eo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

LX=${LX:-lx}
if ! command -v "$LX" >/dev/null 2>&1 && ! [ -x "$LX" ]; then
  echo skip building lxglobals.h
  exit 0
fi

TARGET=./include/lx/lxglobals.h
OBJ="$ROOT/out/lxglobals-new.lxobj"

mkdir -p "$ROOT/out"

echo "#include <stdint.h>" > $TARGET
echo "const uint8_t lxglobals_bytecode[] = {" >> $TARGET
# If $LX is the in-repo compiler (`out/lx`), use fast-path compile.
# Otherwise, use the driver pipeline to avoid bootstrapping mismatches.
if [[ "$LX" == *"/out/lx" || "$LX" == "out/lx" || "$LX" == "./out/lx" ]]; then
  if ! $LX compile lx/globals.lx > "$OBJ"; then
    $LX run lx/scripts/bootstrap-codegen.lx lx/globals.lx
    cp /tmp/globals.lxobj "$OBJ"
  fi
else
  $LX run lx/scripts/bootstrap-codegen.lx lx/globals.lx
  cp /tmp/globals.lxobj "$OBJ"
fi
xxd -i < "$OBJ" >> $TARGET
echo "};" >> $TARGET
