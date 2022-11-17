#!/bin/sh

set -eo pipefail

cd lxlox

echo "#include <stdint.h>" > ../lxlx.h
echo "const uint8_t lxlx_bytecode[] = {" >> ../lxlx.h
lx compile --debug src/main.lx | xxd -r -p | xxd -e -i >> ../lxlx.h
echo "};" >> ../lxlx.h
