CC = gcc
CFLAGS = -g -Wall -Wextra

GTKCFLAGS = `pkg-config --libs --cflags gtk+-3.0`
LDFLAGS = `pkg-config --libs gtk+-3.0`

all:
	make cli
	make gui

cli:
	$(CC) $(CFLAGS) src/cli.c -o build/cli

gui:
	$(CC) $(CFLAGS) $(GTKCFLAGS) src/gui.c -o build/gui $(LDFLAGS)