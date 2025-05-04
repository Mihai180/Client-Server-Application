CC := gcc
CFLAGS := -Wall -Wextra -std=c11 -g

all: server subscriber

server: server.c server_utils.c
	$(CC) $(CFLAGS) -o server server.c server_utils.c

subscriber: subscriber.c server_utils.c
	$(CC) $(CFLAGS) -o subscriber subscriber.c server_utils.c

clean:
	rm -f server subscriber

.PHONY: all clean
