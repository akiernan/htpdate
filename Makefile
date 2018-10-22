prefix = /usr/local
bindir = ${exec_prefix}/bin
mandir = ${prefix}/man

CC = gcc
CFLAGS = -Wall -Os

INSTALL = /usr/bin/install -c

all: htpdate

htpdate: htpdate.c
	$(CC) $(CFLAGS) -o htpdate htpdate.c

install: all
	$(INSTALL) -m 755 htpdate $(bindir)/htpdate
	$(INSTALL) -m 644 htpdate.8.gz $(mandir)/man8/htpdate.8.gz

clean:
	rm -rf htpdate
