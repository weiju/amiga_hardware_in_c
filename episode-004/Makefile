CC=vc +kick13
CFLAGS=-I$(NDK_INC) -c99 -O2 -I../include
LDFLAGS=-lamiga -lauto
EXES=fishtank

.PHONY : clean check
.SUFFIXES : .o .c

all: $(EXES)

clean:
	rm -f *.o $(EXES)

.c.o:
	$(CC) $(CFLAGS) $^ -c -o $@

fishtank: fishtank.o tilesheet.o
	$(CC) $^ $(LDFLAGS) -o $@


fishtank.o: fishtank.c
	$(CC) $(CFLAGS) $^ -c -o $@

tilesheet.o: tilesheet.c
	$(CC) $(CFLAGS) $^ -c -o $@

