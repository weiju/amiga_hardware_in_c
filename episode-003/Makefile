CC=vc +kick13
CFLAGS=-I$(NDK_INC) -c99 -O2 -I../include # -DSPLIT_SCREEN #-DINTERLEAVED
LDFLAGS=-lamiga -lauto
EXES=playfield_ni playfield_split_ni playfield_il playfield_split_il

.PHONY : clean check
.SUFFIXES : .o .c

all: $(EXES)

clean:
	rm -f *.o $(EXES)

.c.o:
	$(CC) $(CFLAGS) $^ -c -o $@

playfield_ni: playfield_ni.o tilesheet.o
	$(CC) $^ $(LDFLAGS) -o $@

playfield_il: playfield_il.o tilesheet.o
	$(CC) $^ $(LDFLAGS) -o $@

playfield_split_ni: playfield_split_ni.o tilesheet.o
	$(CC) $^ $(LDFLAGS) -o $@

playfield_split_il: playfield_split_il.o tilesheet.o
	$(CC) $^ $(LDFLAGS) -o $@


playfield_ni.o: playfield.c
	$(CC) $(CFLAGS) $^ -c -o $@

playfield_il.o: playfield.c
	$(CC) $(CFLAGS) -DINTERLEAVED $^ -c -o $@

playfield_split_ni.o: playfield.c
	$(CC) $(CFLAGS) -DSPLIT_SCREEN $^ -c -o $@

playfield_split_il.o: playfield.c
	$(CC) $(CFLAGS) -DSPLIT_SCREEN -DINTERLEAVED $^ -c -o $@
