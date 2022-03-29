

CC=gcc
CFLAGS=-g -Wall -Wextra -lm -std=gnu99

default: all
all: etd

run: etd
	./$^

input_code: src/input_code.c
	$(CC) $(CFLAGS) -o $@ $^
	./$@
	
etd: src/etd.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f *.o etd *.h.gch

