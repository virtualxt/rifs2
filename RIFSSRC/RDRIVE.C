#include <stdio.h>
#include <stdlib.h>
#include <dos.h>
#include <string.h>
#include <ctype.h>

#include "ifs.h"
#include "rifs.h"
#include "rclient.h"

/*
  RDRIVE /local=a /remote=b
  RDRIVE /local=a
  RDRIVE /remove
*/
static char *options[]={ "/local=",
                         "/remote=",
                         "/remove",
                         NULL};

int main(int argc, char **argv)
{
  int RIFS=FindRIFS();
  struct REGPACK regs;
  char local=0,
       remote=0;
  BOOL removeflag=FALSE;
  int  opt;

  ArgInit(argc, argv);
  if (RIFS) {
    regs.r_ax=0x0000;
    intr(RIFS, &regs);
  }
  if (!RIFS || (regs.r_ax != 0x1234))
    ArgError("RCLIENT not loaded");

  while ((opt=GetOption(options)) != -1) {
    switch (opt) {
      case 0:
        if (local)
          ArgError("/local already defined");
        if (strlen(GetArg()) != 1)
          ArgError("syntax error");
        local=*GetArg();
        if (!isalpha(local))
          ArgError("syntax error");
        break;
      case 1:
        if (remote)
          ArgError("/remote already defined");
        if (strlen(GetArg()) != 1)
          ArgError("syntax error");
        remote=*GetArg();
        if (!isalpha(remote))
          ArgError("syntax error");
        break;
      case 2:
        removeflag=1;
        break;
    }
  }
  if (removeflag) {
    if (remote)
      ArgError("syntax error");
    else if (local) {
      regs.r_ax=RCLIENT_UNMAP;
      regs.r_bx=local;
      intr(RIFS, &regs);
    } else {
      regs.r_ax=RCLIENT_UNMAPALL;
      intr(RIFS, &regs);
    }
  } else if (local) {
    if (!remote)
      ArgError("syntax error");
    else {
      regs.r_ax=RCLIENT_REMAP;
      regs.r_bx=(((unsigned) local) << 8) | remote;
      intr(RIFS, &regs);
    }
  } else {
    int ii;
    BYTE *xlate;
    regs.r_ax=RCLIENT_GETXLAT;
    intr(RIFS, &regs);
    xlate=MK_FP(regs.r_es, regs.r_bx);
    printf("Current drive mappings:\n"
           "  Local --> Remote\n");
    for (ii=0; ii < 26; ii++) {
      if (xlate[ii]) {
        printf("   %c:        %c:\n",
          'A'+ii,
          xlate[ii]);
      }
    }
  }
  return 0;
}