CC=gcc
CFLAGS=-Wall -W -pedantic -ansi -std=gnu11

%: %.c
	$(CC) $(CFLAGS) -o builds/$@ $<

install: main.c
	$(CC) $(CFLAGS) -o builds/main main.c
	cp builds/main ~/bin/unidls
