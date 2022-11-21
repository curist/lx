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

public:
	@mkdir -p public

prepare: out lxlx lxglobals lxversion

build: prepare
	$(CC) -DDEBUG *.c -o out/lx

release: prepare
	$(CC) -Wall -O3 *.c -o out/lx

wasm: prepare
	zig cc -DWASM -D_WASI_EMULATED_PROCESS_CLOCKS \
		-lwasi-emulated-process-clocks \
		-target wasm32-wasi *.c -o out/lx.wasm

EMFLAGS=-sASYNCIFY -sINVOKE_RUN=0 -sENVIRONMENT=web \
				-sEXPORT_ES6 -sMODULARIZE -sEXPORTED_FUNCTIONS=_runRepl
emcc: public prepare
	emcc -Wall -O3 $(EMFLAGS) *.c -o public/lx.js
	cp emscripten/* public/

run: build
	./out/lx run /tmp/current.lxobj

