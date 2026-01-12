.DEFAULT_GOAL := build

LX ?= lx
GITHASH = $(shell git rev-parse --short HEAD)
ARCH = $(shell uname)
DATE = $(shell date "+%Y.%m.%d")

CFLAGS += -D_XOPEN_SOURCE=700
CFLAGS += -Wall -Wextra -Werror -Wno-unused-parameter
CFLAGS += -Iinclude
CFLAGS += -lm -lz
ifeq ($(MODE),debug)
	CFLAGS += -DDEBUG -O0
else
	CFLAGS += -O3
endif

# All .lx sources needed to build embedded compiler/globals bytecode.
# Excludes tests/docs/examples to avoid unnecessary rebuilds.
LX_LX_SOURCES := $(shell find lx \
	\( -path 'lx/test' -o -path 'lx/docs' -o -path 'lx/examples' \) -prune -o \
	-name '*.lx' -print)

include/lx:
	@mkdir -p include/lx

# Generate bundled builtin hover docs data
lx/scripts/builtins_docs_data.lx: API.md include/native_fn.h lx/globals.lx scripts/gen-builtin-docs.lx
	@if ! command -v "$(LX)" >/dev/null 2>&1; then \
		echo "Skipping builtin docs data (no lx on PATH)."; \
		touch $@; \
	else \
		echo "Generating builtin docs data..."; \
		$(LX) run scripts/gen-builtin-docs.lx; \
		echo "Done\n"; \
	fi

include/lx/lxlx.h: $(LX_LX_SOURCES) builtindocs include/chunk.h | include/lx
	./scripts/build-lxlx.sh

include/lx/lxglobals.h: $(LX_LX_SOURCES) include/chunk.h | include/lx
	./scripts/build-globals.sh

lxlx: include/lx/lxlx.h

lxglobals: include/lx/lxglobals.h

builtindocs: lx/scripts/builtins_docs_data.lx

lxversion:
	@echo "const char* LX_VERSION = \"$(DATE)-$(GITHASH) ($(ARCH))\";" > include/lx/lxversion.h

out:
	@mkdir -p out

prepare: out lxlx lxglobals lxversion

build: prepare
	$(CC) src/*.c -o out/lx $(CFLAGS)

benchmark: build
	@cd benchmarks && LX="$(PWD)/out/lx" ./run.sh

clean:
	rm -rf out
	rm include/lx/*

test: build
	@cd lx && make runall && make test
