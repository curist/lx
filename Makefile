.DEFAULT_GOAL := build
# let's use https://github.com/siu/minunit to unit test some

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
	gcc -DDEBUG *.c -o out/clox

release: prepare
	gcc -Wall -O3 *.c -o out/clox

run: build
	./out/clox run /tmp/current.lxobj

