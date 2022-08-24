#include <stdio.h>
#include <stdlib.h>

#include "crc32.h"

void main(int argc, char **argv)
{
  int   ii;
  char *buf=malloc(16384);
  int   read;

  for (ii=1; ii < argc; ii++) {
    FILE *f=fopen(argv[ii], "rb");
    long totalread=0;
    unsigned long CRC=0;
    if (f) {
      do {
        read=fread(buf, 1, 16384, f);
        totalread += read;
        CRC  = crc32(CRC, buf, read);
      } while (read == 16384);
      fclose(f);
      printf("File (%s) Length (%ld) CRC32 (%lx)\n",
        argv[ii], totalread, CRC);
    } else
      printf("File (%s) cannot open\n", argv[ii]);
  }
  free(buf);
}



