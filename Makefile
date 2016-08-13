USE=-DUSE_GLX11
OPT=-g -O0
STD=-std=gnu99
CFLAGS=$(OPT) $(STD) -Wall -Igl3w/include $(USE)
LINK=-lm -lX11 -lGL -lrt -Wall
BIN=l4 mkatlas

all: $(BIN) default.atls

dynary.o: dynary.c dynary.h
	$(CC) $(CFLAGS) -c $<

lsl_prg.o: lsl_prg.c lsl_prg.h
	$(CC) $(CFLAGS) -c $<

l4.o: l4.c
	$(CC) $(CFLAGS) -c $<

l4d.o: l4d.c
	$(CC) $(CFLAGS) -c $<

mkatlas: mkatlas.c
	$(CC) $(CFLAGS) $(LINK) $^ -o $@

default.atls: mkatlas
	./mkatlas default.atls ter-u18n.bdf ter-u12n.bdf ter-u14b.bdf

l4: l4.o l4d.o dynary.o lsl_prg.o
	$(CC) $(LINK) $^ -o $@

clean:
	rm -f *.o $(BIN) default.atls

