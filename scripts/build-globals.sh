#!/bin/sh

set -eo pipefail

if [ -x lx ]; then
  echo skip building lxglobals.h
  exit 0
fi

echo "#include <stdint.h>" > ./lxglobals.h
echo "const uint8_t lxglobals_bytecode[] = {" >> ./lxglobals.h
lx compile --debug ./lxglobals.lx | xxd -r -p | xxd -e -i >> ./lxglobals.h
echo "};" >> ./lxglobals.h
