srcdir=.

LIBS= -lpopt -lz   -lnfs
CC=gcc
CFLAGS=-g -O2 -Wall -W

OBJS = nfs-repl.o libnfs-glue.o

all: nfs-repl

nfs-repl: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LIBS)

clean:
	rm -f *.o *~ nfs-repl
