# Variables
CC = gcc
CFLAGS = -Wall -Werror -pedantic -std=gnu18 -O
LOGIN = nlu
SUBMITPATH = ~cs537-1/handin/$(LOGIN)/P3

# Targets
.PHONY: all

all: wsh

wsh: wsh.c wsh.h
	$(CC) $(CFLAGS) $^ -o $@

run: wsh
	./wsh

pack: $(LOGIN).tar.gz

$(LOGIN).tar.gz: wsh.c wsh.h Makefile README.md
	tar -czvf $@ $^

submit: pack
	cp $(LOGIN).tar.gz $(SUBMITPATH)

clean:
	rm -f wsh $(LOGIN).tar.gz

.PHONY: clean