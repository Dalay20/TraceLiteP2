
CC = gcc
CFLAGS = -Wall -Wextra -g

all: main collector

main: main.c services.c services.h span.h
	$(CC) $(CFLAGS) -o main main.c services.c

collector: collector.c span.h
	$(CC) $(CFLAGS) -o collector collector.c

clean:
	rm -f main collector