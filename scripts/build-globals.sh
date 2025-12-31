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
  if ! $LX compile ./globals.lx > "$OBJ"; then
    $LX run scripts/build-globals-driver.lx > /dev/null
  fi
else
  $LX run scripts/build-globals-driver.lx > /dev/null
fi
xxd -i < "$OBJ" >> $TARGET
echo "};" >> $TARGET
