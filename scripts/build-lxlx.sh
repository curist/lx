#!/bin/bash

set -eo pipefail

if ! [ -x "$(command -v lx)" ]; then
  echo skip building lxlx.h
  exit 0
fi

cd lx

TARGET=../include/lx/lxlx.h
LX=${LX:-lx}

echo "#include <stdint.h>" > $TARGET
echo "const uint8_t lxlx_bytecode[] = {" >> $TARGET
$LX compile src/main.lx | xxd -e -i >> $TARGET
echo "};" >> $TARGET
