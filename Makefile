.DEFAULT_GOAL := build
# let's use https://github.com/siu/minunit to unit test some
prepare:
	@mkdir -p out

build: prepare
	gcc -DDEBUG *.c -o out/clox

release: prepare
	gcc -O3 *.c -o out/clox

run: build
	./out/clox /tmp/current.lxobj

