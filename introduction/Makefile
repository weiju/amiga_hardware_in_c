CC = vc +kick13
CFLAGS=  -I$(NDK_INC) -c99 -O2
LDFLAGS=-lamiga -lauto

.PHONY : clean check
.SUFFIXES : .o .c

all: waitmouse

clean:
	rm -f *.o startup

.c.o:
	$(CC) $(CFLAGS) $^ -c -o $@

waitmouse: waitmouse.o
	$(CC) $^ $(LDFLAGS) -o $@

