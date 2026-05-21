CFLAGS=-Wall -I. -O2 -DNDEBUG -std=c99
SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)
HDRS=$(wildcard *.h)
TARGET=sqlite-bench
SQLITE4_CFLAGS=
SQLITE4_LDFLAGS=-lsqlite4
SQLITE3_RUNNER_LDFLAGS=sqlite3.o -lpthread -ldl -lm

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	LDFLAGS=-lpthread -ldl -lm
else
	LDFLAGS=-lpthread -ldl -lm -static
endif


$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o : %.c 
	$(CC) $(CFLAGS) -c $<

$(OBJS): $(HDRS)

bench: $(TARGET) clean-db
	./$(TARGET)

sqlite4-runner: tools/sqlite4_runner.c
	$(CC) $(CFLAGS) $(SQLITE4_CFLAGS) tools/sqlite4_runner.c -o sqlite4-runner $(SQLITE4_LDFLAGS)

sqlite3-runner: tools/sqlite3_runner.c sqlite3.o sqlite3.h
	$(CC) $(CFLAGS) tools/sqlite3_runner.c -o sqlite3-runner $(SQLITE3_RUNNER_LDFLAGS)

clean:
	rm -f $(TARGET) sqlite3-runner sqlite4-runner *.o

clean-db:
	rm -f dbbench_sqlite3*

.PHONY: bench clean clean-db sqlite3-runner sqlite4-runner
