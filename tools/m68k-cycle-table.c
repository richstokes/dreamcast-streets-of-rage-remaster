/* Print the Musashi MC68000 base cycle table (Genesis Plus GX research checkout)
   as 65,536 decimal CPU-cycle counts, one per opcode. Host tool only. */
#include <stdio.h>
/* Genesis Plus GX stores the table scaled by MUL, in master-clock units;
   dividing it back out gives 68000 cycles. */
#define MUL 7
#include "m68ki_cycles.h"
int main(void) {
    for (unsigned op = 0; op < 0x10000; op++) printf("%u\n", m68ki_cycles[op] / MUL);
    return 0;
}
