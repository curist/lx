.DEFAULT_GOAL := build

GITHASH = $(shell git rev-parse --short HEAD)
ARCH = $(shell uname)
DATE = $(shell date "+%Y.%m.%d")

CFLAGS += -Wall -Wextra -Werror -Wno-unused-parameter
CFLAGS += -Iinclude -I.
ifeq ($(MODE),debug)
	CFLAGS += -DDEBUG -O0
else
	CFLAGS += -O3 -flto
endif

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
	$(CC) $(CFLAGS) src/*.c -o out/lx

wasm: prepare
	zig cc $(CFLAGS) -DWASM -target wasm32-wasi src/*.c -o out/lx.wasm

EMFLAGS=-sASYNCIFY -sINVOKE_RUN=0 -sENVIRONMENT=web \
				-sEXPORT_ES6 -sMODULARIZE -sEXPORTED_FUNCTIONS=_runRepl
emcc: prepare
	emcc $(CFLAGS) -DWASM $(EMFLAGS) src/*.c -o docs/lx.js

clean:
	rm -rf out

test:
	@cd lxlx && make runall && make test

