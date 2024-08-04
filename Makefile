CC=gcc
CFLAGS=-Wall -W -pedantic -ansi -std=gnu11

%: %.c
	$(CC) $(CFLAGS) -o builds/$@ $<
