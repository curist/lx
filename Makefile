# let's use https://github.com/siu/minunit to unit test some
prepare:
	@mkdir -p out

build: prepare
	gcc *.c -o out/clox

run: build
	./out/clox

