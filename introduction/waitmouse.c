#include <exec/types.h>

volatile UBYTE *ciaa_pra = (volatile UBYTE *) 0xbfe001;
#define  PRA_FIR0_BIT (1 << 6)

void waitmouse(void)
{
  while ((*ciaa_pra & PRA_FIR0_BIT) != 0) ;
}

int main(int argc, char **argv)
{
	waitmouse();
    return 0;
}

