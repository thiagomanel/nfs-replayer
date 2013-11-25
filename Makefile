srcdir=.

LIBS= -lpopt -lz   -lnfs
CC=gcc
CFLAGS=-g -O2 -Wall -W

OBJS = nfs-repl.o
#libnfs-glue.o socklib.o linux_scsi.o libiscsi.o 

all: nfs-repl

nfs-repl: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LIBS)
#nfsio.o: nfsio.c
#	@echo Compiling $@
#	gcc -g -c nfsio.c -o $@
clean:
	rm -f *.o *~ nfs-repl
