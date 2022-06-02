CC = clang

# compiler flags:
CFLAGS = -Wall -Wextra -Werror -pedantic -g -pthread
DEPS = queue.h
OBJ = httpserver.o queue.o

all: httpserver

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $<

httpserver: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	$(RM) httpserver
	$(RM) $(OBJ)
