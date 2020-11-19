CC=vc +kick13
CFLAGS=-I$(NDK_INC) -c99 -O2 -I../include
LDFLAGS=-lamiga -lauto
EXES=example_00 example_01

.PHONY : clean check
.SUFFIXES : .o .c

all: $(EXES)

clean:
	rm -f *.o $(EXES)

.c.o:
	$(CC) $(CFLAGS) $^ -c -o $@

example_00: example_00.o
	$(CC) $^ $(LDFLAGS) -o $@

example_01: example_01.o tilesheet.o
	$(CC) $^ $(LDFLAGS) -o $@

