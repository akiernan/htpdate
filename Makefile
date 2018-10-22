CC = gcc
CFLAGS = -O2

bindir = /usr/bin

INSTALL = /usr/bin/install -c

BINS = htpdate

all: $(BINS)
htpdate: htpdate.o
	$(CC) $(CFLAGS) -o htpdate htpdate.c

htpdate.o: htpdate.c

install: all
	$(INSTALL) -m 755 htpdate $(bindir)/htpdate
