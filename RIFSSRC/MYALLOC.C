#include <dos.h>

#include "myalloc.h"

/*
  i wrap the standard allocations around
    2 bytes -- head ID 0x1234
    2 bytes -- size (n)
    n bytes -- data
    2 bytes -- tail ID 0x4321
*/
void *my_malloc(unsigned size)
{
  unsigned seg;
  if (allocmem((size+15+6)/16, &seg)) {
    char *buf=MK_FP(seg, 0);
    *(unsigned *) buf=0x1234;
    *(unsigned *) (buf+2)=size;
    *(unsigned *) (4+buf+size)=0x4321;
    return MK_FP(seg, 4);
  } else
    return 0;
}

void my_free(void *buf)
{
  freemem(FP_SEG(buf));
}

/*
  check that [buf] was allocated by me, and currently points
  to a valid part of the allocation
*/
int my_check(void *buf)
{
  int err=0;
  char *mybuf=MK_FP(FP_SEG(buf), 0);
  int size=*(unsigned *) (mybuf+2);

  if (*(unsigned *) mybuf != 0x1234)
    err |= 1;
  if (*(unsigned *) (mybuf+4+size) != 0x4321)
    err |= 2;
  if ((FP_OFF(buf) < 4) || (FP_OFF(buf) > 4+size))
    err |= 4;
  return err;
}