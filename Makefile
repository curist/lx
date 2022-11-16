.DEFAULT_GOAL := build
# let's use https://github.com/siu/minunit to unit test some

GITHASH = $(shell git rev-parse --short HEAD)
ARCH = $(shell uname)
DATE = $(shell date "+%Y.%m.%d")

prepare:
	@mkdir -p out
	@echo "const char* LX_VERSION = \"$(DATE)-$(GITHASH) ($(ARCH))\";" > version.h

build: prepare
	gcc -DDEBUG *.c -o out/clox

release: prepare
	gcc -Wall -O3 *.c -o out/clox

run: build
	./out/clox /tmp/current.lxobj

