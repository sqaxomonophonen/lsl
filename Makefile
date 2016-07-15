USE=-DUSE_GLX11
OPT=-g -O0
STD=-std=gnu99
CFLAGS=$(OPT) $(STD) -Wall -Igl3w/include $(USE)
LINK=-lm -lX11 -lGL -lrt -Wall
BIN=lsl-4th mkatlas

all: $(BIN) default.atls

lsl_prg.o: lsl_prg.c lsl_prg.h
	$(CC) $(CFLAGS) -c $<

lsl_4th.o: lsl_4th.c
	$(CC) $(CFLAGS) -c $<

mkatlas: mkatlas.c
	$(CC) $(CFLAGS) $(LINK) $^ -o $@

default.atls: mkatlas
	./mkatlas default.atls ter-u18n.bdf ter-u12n.bdf ter-u14b.bdf

lsl-4th: lsl_prg.o lsl_4th.o
	$(CC) $(LINK) $^ -o $@

clean:
	rm -f *.o $(BIN) default.atls

