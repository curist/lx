#!/bin/bash

set -eo pipefail

if ! [ -x "$(command -v lx)" ]; then
  echo skip building lxglobals.h
  exit 0
fi

TARGET=./include/lx/lxglobals.h
LX=${LX:-lx}

echo "#include <stdint.h>" > $TARGET
echo "const uint8_t lxglobals_bytecode[] = {" >> $TARGET
$LX compile --debug ./lx/globals.lx | xxd -r -p | xxd -e -i >> $TARGET
echo "};" >> $TARGET
