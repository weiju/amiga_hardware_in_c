CC=vc +kick13
CFLAGS=-I$(NDK_INC) -c99 -O2 -I../include
LDFLAGS=-lamiga -lauto
EXES=example_01 example_02 example_03 example_04

.PHONY : clean check
.SUFFIXES : .o .c

all: $(EXES)

clean:
	rm -f *.o $(EXES)

.c.o:
	$(CC) $(CFLAGS) $^ -c -o $@

example_01: example_01.o tilesheet.o
	$(CC) $^ $(LDFLAGS) -o $@

example_02: example_02.o tilesheet.o
	$(CC) $^ $(LDFLAGS) -o $@

example_03: example_03.o tilesheet.o
	$(CC) $^ $(LDFLAGS) -o $@

example_04: example_04.o tilesheet.o
	$(CC) $^ $(LDFLAGS) -o $@
