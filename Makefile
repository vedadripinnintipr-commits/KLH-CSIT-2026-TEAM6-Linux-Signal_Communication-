CC = gcc
CFLAGS = -Wall -Wextra -O2
GTK_CFLAGS = $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS = $(shell pkg-config --libs gtk+-3.0)

all: process_control worker

process_control: main.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) main.c -o process_control $(GTK_LIBS)

worker: worker.c
	$(CC) $(CFLAGS) worker.c -o worker

clean:
	rm -f process_control worker

.PHONY: all clean
