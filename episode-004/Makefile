CC=vc +kick13
CFLAGS=-I$(NDK_INC) -c99 -O2 -I../include
LDFLAGS=-lamiga -lauto
EXES=fishtank-00 fishtank-01 fishtank

.PHONY : clean check
.SUFFIXES : .o .c

all: $(EXES)

clean:
	rm -f *.o $(EXES)

.c.o:
	$(CC) $(CFLAGS) $^ -c -o $@

fishtank: fishtank.o tilesheet.o
	$(CC) $^ $(LDFLAGS) -o $@

fishtank-01: fishtank-01.o tilesheet.o
	$(CC) $^ $(LDFLAGS) -o $@

fishtank-00: fishtank-00.o tilesheet.o
	$(CC) $^ $(LDFLAGS) -o $@
