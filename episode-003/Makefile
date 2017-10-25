CC = vc +kick13
CFLAGS=  -I$(NDK_INC) -c99 -O2 -I../include #-DINTERLEAVED
LDFLAGS=-lamiga -lauto

.PHONY : clean check
.SUFFIXES : .o .c

all: playfield

clean:
	rm -f *.o playfield

.c.o:
	$(CC) $(CFLAGS) $^ -c -o $@

playfield: playfield.o tilesheet.o
	$(CC) $^ $(LDFLAGS) -o $@

