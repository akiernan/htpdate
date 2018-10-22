prefix = /usr
bindir = ${prefix}/bin
mandir = ${prefix}/share/man

CC = gcc
CFLAGS += -Wall -ansi -Os

INSTALL = /usr/bin/install -c
STRIP = /usr/bin/strip -s

all: htpdate

htpdate: htpdate.c
	$(CC) $(CFLAGS) -o htpdate htpdate.c

install: all
	$(STRIP) htpdate
	mkdir -p $(bindir)
	$(INSTALL) -m 755 htpdate $(bindir)/htpdate
	mkdir -p $(mandir)/man8
	$(INSTALL) -m 644 htpdate.8.gz $(mandir)/man8/htpdate.8.gz

clean:
	rm -rf htpdate

uninstall:
	rm -rf $(bindir)/htpdate
	rm -rf $(mandir)/man8/htpdate.8.gz
