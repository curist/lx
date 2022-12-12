.PHONY: main test

TARGET = src/main.lx
# TARGET = src/types.lx
# TARGET = src/compiler.lx
# TARGET = test/stub/ch25.lx
# TARGET = test/stub/sink.lx
# TARGET = test/stub/import.lx
# TARGET = test/stub/functions.lx

LX = ../out/lx

main:
	$(LX) compile --debug $(TARGET) | xxd -r -p > /tmp/current.lxobj
	$(LX) compile $(TARGET)

test:
	@ls test/*.test.lx | xargs -I{} sh -c "echo {} && $(LX) run {}"

dump:
	$(LX) compile --debug test/stub/ch17.lx | xxd -r -p > /tmp/ch17.lxobj &
	$(LX) compile --debug test/stub/ch18.lx | xxd -r -p > /tmp/ch18.lxobj &
	$(LX) compile --debug test/stub/ch19.lx | xxd -r -p > /tmp/ch19.lxobj &
	$(LX) compile --debug test/stub/ch21.lx | xxd -r -p > /tmp/ch21.lxobj &
	$(LX) compile --debug test/stub/ch22.lx | xxd -r -p > /tmp/ch22.lxobj &
	$(LX) compile --debug test/stub/ch23.lx | xxd -r -p > /tmp/ch23.lxobj &
	$(LX) compile --debug test/stub/ch23-1.lx | xxd -r -p > /tmp/ch23-1.lxobj &
	$(LX) compile --debug test/stub/ch24.lx | xxd -r -p > /tmp/ch24.lxobj &
	$(LX) compile --debug test/stub/ch25.lx | xxd -r -p > /tmp/ch25.lxobj &
	$(LX) compile --debug test/stub/hashmap.lx | xxd -r -p > /tmp/hashmap.lxobj &
	$(LX) compile --debug test/stub/array.lx | xxd -r -p > /tmp/array.lxobj &
	$(LX) compile --debug test/stub/sink.lx | xxd -r -p > /tmp/sink.lxobj &

runall:
	$(LX) run /tmp/ch17.lxobj
	$(LX) run /tmp/ch18.lxobj
	$(LX) run /tmp/ch19.lxobj
	$(LX) run /tmp/ch21.lxobj
	$(LX) run /tmp/ch22.lxobj
	$(LX) run /tmp/ch23.lxobj
	$(LX) run /tmp/ch23-1.lxobj
	$(LX) run /tmp/ch24.lxobj
	$(LX) run /tmp/ch25.lxobj
	$(LX) run /tmp/hashmap.lxobj
	$(LX) run /tmp/array.lxobj
	$(LX) run /tmp/sink.lxobj


all: main test dump
