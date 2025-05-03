CC      := gcc
CFLAGS  := -Wall -Wextra -std=c11 -g
LDFLAGS :=
TARGETS := server subscriber

all: $(TARGETS)

server: server.c server_utils.c
	$(CC) $(CFLAGS) -o server server.c server_utils.c $(LDFLAGS)

subscriber: subscriber.c
	$(CC) $(CFLAGS) -o subscriber subscriber.c $(LDFLAGS)

clean:
	rm -f $(TARGETS) *.o

.PHONY: all clean
