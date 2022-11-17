#!/bin/sh

set -eo pipefail

if ! [ -x "$(command -v lx)" ]; then
  echo skip building lxlx.h
  exit 0
fi

cd lxlox

echo "#include <stdint.h>" > ../lx/lxlx.h
echo "const uint8_t lxlx_bytecode[] = {" >> ../lx/lxlx.h
lx compile --debug src/main.lx | xxd -r -p | xxd -e -i >> ../lx/lxlx.h
echo "};" >> ../lx/lxlx.h
