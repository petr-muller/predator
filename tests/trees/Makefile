.PHONY: clean all

all: test-0001 test-0002

test-0001: test-0001.c
	gcc -O0 -ggdb3 test-0001.c -o test-0001 -I../../include/predator-builtins

test-0002: test-0002.c
	gcc -O0 -ggdb3 test-0002.c -o test-0002 -I../../include/predator-builtins

clean:
	rm -f test-0001 test-0002
