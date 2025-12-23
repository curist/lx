.PHONY: test

LX = ../out/lx

$(LX):
	@cd .. && make

runall: $(LX)
	$(LX) compile src/main.lx > /dev/null
	$(LX) run test/stub/ch17.lx
	$(LX) run test/stub/ch18.lx
	$(LX) run test/stub/ch19.lx
	$(LX) run test/stub/ch21.lx
	$(LX) run test/stub/ch22.lx
	$(LX) run test/stub/ch23.lx
	$(LX) run test/stub/ch23-1.lx
	$(LX) run test/stub/ch24.lx
	$(LX) run test/stub/ch25.lx
	$(LX) run test/stub/hashmap.lx
	$(LX) run test/stub/array.lx
	$(LX) run test/stub/sink.lx

test: $(LX)
	@ls test/*.test.lx | xargs -I{} $(LX) run {}
