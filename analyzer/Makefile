# compiler flags
CFLAGS= -O2 -Wall

.PHONY: clean install stats rflib_

all: aldl-analyzer

aldl-analyzer: analyzer.c csv.o loadconfig.o config.h rflib_
	gcc $(CFLAGS) -o aldl-analyzer analyzer.c csv.o loadconfig.o ../rflib/rflib.o

csv.o: csv.c csv.h config.h rflib_
	gcc $(CFLAGS) -c csv.c

loadconfig.o: loadconfig.c loadconfig.h config.h
	gcc $(CFLAGS) -c loadconfig.c

rflib_:
	cd ../rflib ; make ; cd ../analyzer

install: aldl-analyzer analyzer.conf
	cp -nv analyzer.conf /etc/aldl/analyzer.conf
	cp -v aldl-analyzer /usr/local/bin/aldl-analyzer

clean:
	rm -f aldl-analyzer *.o
	cd ../rflib ; make clean ; cd ../analyzer

stats:
	wc -l *.c *.h */*.c */*.h
