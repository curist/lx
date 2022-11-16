.DEFAULT_GOAL := build
# let's use https://github.com/siu/minunit to unit test some

GITHASH = $(shell git rev-parse --short HEAD)
ARCH = $(shell uname)
DATE = $(shell date "+%Y.%m.%d")

lxcompiler:
	./scripts/build-compiler.sh

lxglobals:
	./scripts/build-globals.sh

version:
	@echo "const char* LX_VERSION = \"$(DATE)-$(GITHASH) ($(ARCH))\";" > version.h

prepare: lxcompiler lxglobals version
	@mkdir -p out

build: prepare
	gcc -DDEBUG *.c -o out/clox

release: prepare
	gcc -Wall -O3 *.c -o out/clox

run: build
	./out/clox run /tmp/current.lxobj

