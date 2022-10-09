prepare:
	@mkdir -p out

build: prepare
	gcc main.c -o out/clox

run: build
	./out/clox

