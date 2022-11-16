#!/bin/sh

cd lxlox

sh -c 'echo "#include <stdint.h>" > ../lxcompiler.h'
sh -c 'echo "const uint8_t lxcompiler_bytecode[] = {" >> ../lxcompiler.h'
lx compile --debug src/main.lx | xxd -r -p | xxd -e -i >> ../lxcompiler.h
sh -c 'echo "};" >> ../lxcompiler.h'
