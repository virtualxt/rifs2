#include <stdio.h>
#include <stdlib.h>
#include <dos.h>
#include <time.h>
#include <conio.h>
#include "math.h"
#include "ifs.h"
#include "rifs.h"
#include "rclient.h"

void ShowStat(char *title, IFS_STAT *st)
{
    printf("%s\n"
           "Total bytes sent: %10ld\n"
           "        recieved: %10ld\n"
           "           valid: %10ld\n"
           "                  %13.2f\n"
           "Failures:\n"
           "        timeout:  %10ld\n"
           "        CRC:      %10ld\n"
           "        length:   %10ld\n"
           "Stack used:       %10d\n",
           title,
           st->totalsent, st->totalrcvd, st->valid,
           (float) 100.0 * st->valid/(st->totalrcvd ? st->totalrcvd : 1),
           st->timeout,
           st->crcfail,
           st->lenfail,
           st->stackused);
}

void ShowOpenFile(unsigned handle, unsigned psp)
{
  struct REGPACK regs;
  SYS_FTAB **first,
            *next;
  int handno,
      ii,
      sftlen=(_osmajor == 3) ? 0x35 : 0x3b;

  regs.r_ax=0x5200;
  intr(0x21, &regs);
  first=MK_FP(regs.r_es, regs.r_bx+4);
  next = *first;
  handno=0;
  do {
    for (ii=0; ii < next->num_files; ii++) {
      SFT *t=(SFT *) (next->files + ii*sftlen);
      if (handno == handle) {
        printf("%4x : %11.11s\n",
          psp, t->fcb_name);
        return;
      }
      handno++;
    }
    next=next->next;
  } while (FP_SEG(next) && (FP_OFF(next) != 0xffff));
  printf("%4x : {invalid file handle}\n", psp);
}

void ShowServerStat(char *title, IFS_STAT *st)
{
  int ii, flag=0;

  ShowStat(title, st);
  printf("Number of server calls: %10ld\n", st->inserver);
  for (ii=0; ii < MAXOPEN; ii++) {
    if (st->openpsp[ii]) {
      if (!flag) {
        printf("Open files:\n");
        flag=1;
      }
      ShowOpenFile(st->openhandle[ii], st->openpsp[ii]);
    }
  }
  if (!flag)
    printf("No Open Files\n");
}

void main(void)
{
  int RIFS=FindRIFS();
  struct REGPACK regs;
  int key=0;
  long now=0, last=0;

  if (!RIFS) {
    fprintf(stderr, "Cannot locate RIFS\n");
    exit(1);
  }
  clrscr();
  do {
    time(&now);
    if (labs(now-last) >= 5) {
      last=now;
      gotoxy(1,1);
      regs.r_ax=RCLIENT_GETSTAT;
      intr(RIFS, &regs);
      if (!(regs.r_flags & 0x01))
        ShowStat("RCLIENT", MK_FP(regs.r_es, regs.r_bx));
      regs.r_ax=RSERVER_GETSTAT;
      intr(RIFS, &regs);
      if (!(regs.r_flags & 0x01)) {
        ShowServerStat("RSERVER", MK_FP(regs.r_es, regs.r_bx));
      }
    }
    if (kbhit())
      key=getch();
  } while (key != 27);
}
