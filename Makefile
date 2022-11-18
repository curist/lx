.DEFAULT_GOAL := build

GITHASH = $(shell git rev-parse --short HEAD)
ARCH = $(shell uname)
DATE = $(shell date "+%Y.%m.%d")

lxlx:
	./scripts/build-lxlx.sh

lxglobals:
	./scripts/build-globals.sh

lxversion:
	@echo "const char* LX_VERSION = \"$(DATE)-$(GITHASH) ($(ARCH))\";" > lx/lxversion.h

out:
	@mkdir -p out

prepare: out lxlx lxglobals lxversion

build: prepare
	cc -DDEBUG *.c -o out/lx

wasm: prepare
	zig cc -DWASM -D_WASI_EMULATED_PROCESS_CLOCKS \
		-lwasi-emulated-process-clocks \
		-target wasm32-wasi *.c -o out/lx.wasm

release: prepare
	cc -Wall -O3 *.c -o out/lx

run: build
	./out/lx run /tmp/current.lxobj

