#!/bin/bash

set -eo pipefail

if ! [ -x "$(command -v lx)" ]; then
  echo skip building lxglobals.h
  exit 0
fi

echo "#include <stdint.h>" > ./lx/lxglobals.h
echo "const uint8_t lxglobals_bytecode[] = {" >> ./lx/lxglobals.h
lx compile --debug ./lx/lxglobals.lx | xxd -r -p | xxd -e -i >> ./lx/lxglobals.h
echo "};" >> ./lx/lxglobals.h
