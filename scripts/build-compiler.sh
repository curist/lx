#!/bin/sh

set -eo pipefail

cd lxlox

echo "#include <stdint.h>" > ../lxcompiler.h
echo "const uint8_t lxcompiler_bytecode[] = {" >> ../lxcompiler.h
lx compile --debug src/main.lx | xxd -r -p | xxd -e -i >> ../lxcompiler.h
echo "};" >> ../lxcompiler.h
