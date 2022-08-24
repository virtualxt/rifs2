/*
  this program will have two simple functions.
    CLIENT
      1. check IFS request. if not for my drives, pass on
      2. packet the request for xfer
      3. xfer packet. wait for return
      4. unpacket the result
      5. return
*/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <dir.h>
#include <dos.h>

#include "ifs.h"
#include "comio.h"
#include "crc32.h"
#include "svr0.h"
#include "ifs0.h"
#include "rifs.h"
#include "myalloc.h"
#include "rclient.h"

unsigned _stklen=512;

#define LOCAL static

LOCAL BYTE    *SDA;         /* swappable data area         */
LOCAL WORD     SDA_maxsize; /* size of SDA                 */
LOCAL WORD     SDA_minsize;
LOCAL BYTE    *CDS_base;    /* current directory structure */
LOCAL WORD     CDS_size;    /* # of bytes per entry        */
LOCAL WORD     CDS_ct;      /* number of entries           */

/*************************************************************************
 *************************************************************************
  CLIENT
 *************************************************************************
 *************************************************************************/
/*
  these are the sda members about which i am concerned
  (set on entry)
*/
LOCAL WORD  *SDA_CURR_PSP;
LOCAL DTA  **SDA_CURR_DTA;
LOCAL char  *SDA_FN1;
LOCAL char  *SDA_FN2;
LOCAL SDB   *SDA_SDB;
LOCAL CDS   *SDA_CURR_CDS;
LOCAL DIR   *SDA_DIR_ENTRY;
LOCAL BYTE  *SDA_SRCH_ATTR;
LOCAL BYTE  *SDA_OPEN_MODE;
LOCAL CDS  **SDA_DRIVE_CDSPTR;
LOCAL SFT  **SDA_SFT;
LOCAL WORD  *SDA_EXTOPEN_ACTION;
LOCAL WORD  *SDA_EXTOPEN_ATTR;
LOCAL WORD  *SDA_EXTOPEN_MODE;

LOCAL CDS     *CDS_init;          /* initial CDS structure       */
LOCAL XMITBUF *iobuf1,            /* current / last used buffers */
              *iobuf2,
              *iobufptr;          /* current buffer        */
LOCAL WORD     iobufptr_datasize; /* i/o data buffer size  */
LOCAL WORD     iobufptr_rwdatasize; /* largest block to send for read/write
                                       must be no bigger than datasize-8  */
LOCAL BYTE     remote;            /* remote drive map      */
LOCAL BYTE     local;             /* local drive           */

/*
  drive translation table
  0 = not mapped, else 'A'..'Z' is translation
  ex:
    SVR_XLAT_TABLE[3] == 'C'
      then local drive D is mapped to server drive C
*/
LOCAL BYTE DRIVE_XLAT_TABLE[26];

LOCAL IFS_STAT IFS_stat;

static void uninit(void)
{
  ifs0_shutdown();
  CommIO_shutdown();
  if (CDS_init) {
    memcpy(CDS_base, CDS_init, CDS_size * CDS_ct);
    my_free(CDS_init);
    CDS_init=0;
  }
  if (iobuf1) {
    my_free(iobuf1);
    my_free(iobuf2);
    iobufptr=0;
    iobufptr_datasize=0;
  }
}

/*
  initialize misc-> varaibles
*/
static BOOL InitClient(int intno)
{
  struct REGPACK regs;
  BYTE *LOL; /* list of lists       */

  regs.r_ax=0x5d06;
  intr(0x21, &regs);
  SDA=MK_FP(regs.r_ds, regs.r_si);
  SDA_maxsize=regs.r_cx;
  SDA_minsize=regs.r_dx;

  /*
    init lcl_SDA (for SERVER)
  */
  regs.r_ax=0x5200;
  intr(0x21, &regs);
  LOL=MK_FP(regs.r_es, regs.r_bx);

  CDS_base=*(void **) (LOL+0x16);
  CDS_ct=*(BYTE *) (LOL + 0x21);

  if (_osmajor >= 5) {
    CDS_size           = 0x58;
    SDA_CURR_DTA       = (void *) (SDA + 0x000c);
    SDA_CURR_PSP       = (void *) (SDA + 0x0010);
    SDA_FN1            = (void *) (SDA + 0x009e);
    SDA_FN2            = (void *) (SDA + 0x011e);
    SDA_SDB            = (void *) (SDA + 0x019e);
    SDA_DIR_ENTRY      = (void *) (SDA + 0x01b3);
    SDA_CURR_CDS       = (void *) (SDA + 0x01d3);
    SDA_SRCH_ATTR      = (void *) (SDA + 0x024d);
    SDA_OPEN_MODE      = (void *) (SDA + 0x024e);
    SDA_SFT            = (void *) (SDA + 0x027e);
    SDA_DRIVE_CDSPTR   = (void *) (SDA + 0x0282);
    SDA_EXTOPEN_ACTION = (void *) (SDA + 0x02dd);
    SDA_EXTOPEN_ATTR   = (void *) (SDA + 0x02df);
    SDA_EXTOPEN_MODE   = (void *) (SDA + 0x02e1);
  } else {
    CDS_size           = 0x51;
    SDA_CURR_DTA       = (void *) (SDA + 0x000c);
    SDA_CURR_PSP       = (void *) (SDA + 0x0010);
    SDA_FN1            = (void *) (SDA + 0x0092);
    SDA_FN2            = (void *) (SDA + 0x0112);
    SDA_SDB            = (void *) (SDA + 0x0192);
    SDA_DIR_ENTRY      = (void *) (SDA + 0x01a7);
    SDA_CURR_CDS       = (void *) (SDA + 0x01c7);
    SDA_SRCH_ATTR      = (void *) (SDA + 0x023a);
    SDA_OPEN_MODE      = (void *) (SDA + 0x023b);
    SDA_SFT            = (void *) (SDA + 0x0268);
    SDA_DRIVE_CDSPTR   = (void *) (SDA + 0x026c);
    SDA_EXTOPEN_ACTION = 0; /* these do not exist in DOS 3.x */
    SDA_EXTOPEN_ATTR   = 0;
    SDA_EXTOPEN_MODE   = 0;
  }
  ifs0_init(intno);

  CDS_init=my_malloc(CDS_size * CDS_ct);
  memcpy(CDS_init, CDS_base, CDS_size * CDS_ct);

  iobufptr_datasize=BLOCKSIZE;
  iobufptr_rwdatasize=BLOCKSIZE-8;
  iobuf1=my_malloc(iobufptr_datasize+sizeof(*iobuf1));
  iobuf2=my_malloc(iobufptr_datasize+sizeof(*iobuf2));
  iobufptr=iobuf1;
  return 1;
}

#define COMMAND(a) {iobufptr->length=0; iobufptr->cmd=a; }
#define SPARAM(a) {strcpy(iobufptr->data+iobufptr->length, a); iobufptr->length+=strlen(a)+1; }

WORD Transmit(void);

/*
  remove directory
*/
static BOOL ifs_rmdir(INTREGS *regs)
{
  COMMAND(IFS_RMDIR);
  SPARAM(SDA_FN1);
  iobufptr->data[0]=remote;
  Transmit();
  regs->ax=iobufptr->cmd;
  return (regs->ax) ? 0xff00 : 0x0000;
}

/*
  change directory
*/
static BOOL ifs_chdir(INTREGS *regs)
{
  COMMAND(IFS_CHDIR);
  SPARAM(SDA_FN1);
  iobufptr->data[0]=remote;
  Transmit();
  regs->ax=iobufptr->cmd;
  return (regs->ax) ? 0xff00 : 0x0000;
}

/*
  create directory
*/
static BOOL ifs_mkdir(INTREGS *regs)
{
  COMMAND(IFS_MKDIR);
  SPARAM(SDA_FN1);
  iobufptr->data[0]=remote;
  Transmit();
  regs->ax=iobufptr->cmd;
  return (regs->ax) ? 0xff00 : 0x0000;
}

static BOOL ifs_closefile(INTREGS *regs)
{
  SFT *sft=MK_FP(regs->es, regs->di);

  COMMAND(IFS_CLOSEFILE);
  *(int *) iobufptr->data=sft->dir_sector;
  iobufptr->length=2;
  Transmit();

  if (sft->handlect > 0)
    sft->handlect--;

  regs->ax=iobufptr->cmd;
  return (regs->ax) ? 0xff00 : 0x0000;
}

static BOOL ifs_commitfile(INTREGS *regs)
{
  COMMAND(IFS_COMMITFILE);
  Transmit();
  regs->ax=iobufptr->cmd;
  return (regs->ax) ? 0xff00 : 0x0000;
}

#define NORMALIZE(a) a=MK_FP(FP_SEG(a)+(FP_OFF(a) >> 4), FP_OFF(a) & 0x0f)

static BOOL ifs_readfile(INTREGS *regs)
{
  SFT *sft=MK_FP(regs->es, regs->di);
  char *dst=(char *) *SDA_CURR_DTA;
  WORD  sz=regs->cx;
  int   read;

  regs->cx=0;
  while (sz > 0) {
    int tz=min(iobufptr_rwdatasize, sz);

    COMMAND(IFS_READFILE);
    *(int  *) iobufptr->data    =sft->dir_sector;
    *(long *) (iobufptr->data+2)=sft->fpos;
    *(int  *) (iobufptr->data+6)=tz;
    iobufptr->length=8;

    Transmit();

    read=*(int *) iobufptr->data;
    memcpy(dst, iobufptr->data+2, read);

    dst += read;
    NORMALIZE(dst);

    sz -= read;
    regs->cx += read;

    regs->ax=iobufptr->cmd;

    sft->fpos += read;

    if ((regs->ax != 0) || (read != tz))
      break;
  }
  return (regs->ax) ? 0xff00 : 0x0000;
}

static BOOL ifs_writefile(INTREGS *regs)
{
  SFT *sft=MK_FP(regs->es, regs->di);
  char *src=(char *) *SDA_CURR_DTA;
  WORD  sz=regs->cx;
  int   trunc = (regs->cx == 0); /* if write length is 0, truncate file */
  int   written;

  regs->cx=0;
  while ((sz > 0) | trunc) {
    int tz=min(iobufptr_rwdatasize, sz); /* 8 = size of return header */

    COMMAND(IFS_WRITEFILE);
    *(int  *) iobufptr->data     = sft->dir_sector;
    *(long *) (iobufptr->data+2) = sft->fpos;
    *(int  *) (iobufptr->data+6) = tz;
    iobufptr->length=8;
    if (tz) {
      memcpy(iobufptr->data+iobufptr->length, src, tz);
      iobufptr->length+=tz;
    } else
      trunc=0;
    Transmit();

    written=*(int *) iobufptr->data;
    src += written;
    NORMALIZE(src);
    sz -= *(int *)  iobufptr->data;
    regs->cx += written;

    regs->ax=iobufptr->cmd;

    sft->fpos += written;
    sft->fsize = max(sft->fsize, sft->fpos);
    sft->dev_info &= ~0x0040;

    /*
      on error or partial, break
    */
    if ((regs->ax != 0) || (written != tz))
      break;
  }
  return (regs->ax) ? 0xff00 : 0x0000;
}

static BOOL ifs_lockfile(INTREGS *regs)
{
  return (regs->ax) ? 0xff00 : 0x0000;
}

static BOOL ifs_unlockfile(INTREGS *regs)
{
  regs->ax=0x05;
  return (regs->ax) ? 0xff00 : 0x0000;
}

static BOOL ifs_getspace(INTREGS *regs)
{
  struct dfree *df;
  COMMAND(IFS_GETSPACE);
  *(BYTE *) iobufptr->data=(remote-'A'+1);
  iobufptr->length=1;
  Transmit();
  regs->ax=iobufptr->cmd;
  df=(void *) iobufptr->data;
  regs->ax=df->df_sclus;
  regs->bx=df->df_total;
  regs->cx=df->df_bsec;
  regs->dx=df->df_avail;
  return 0;
}

static BOOL ifs_setattr(INTREGS *regs)
{
  COMMAND(IFS_SETATTR);
  *(WORD *) (iobufptr->data)=regs->wparam;
  iobufptr->length=2;
  SPARAM(SDA_FN1);
  iobufptr->data[2]=remote;
  Transmit();
  regs->ax=iobufptr->cmd;
  return (regs->ax) ? 0xff00 : 0x0000;
}

static BOOL ifs_getattr(INTREGS *regs)
{
  COMMAND(IFS_GETATTR);
  SPARAM(SDA_FN1);
  iobufptr->data[0]=remote;
  Transmit();
  regs->ax=*(WORD *) iobufptr->data;
  return (iobufptr->cmd) ? 0xff00 : 0x0000;
}

static BOOL ifs_renamefile(INTREGS *regs)
{
  char *ptr;

  COMMAND(IFS_RENAMEFILE);
  SPARAM(SDA_FN1);
  iobufptr->data[0]=remote;
  ptr=iobufptr->data+iobufptr->length;
  SPARAM(SDA_FN2);
  *ptr=remote;
  Transmit();
  regs->ax=iobufptr->cmd;
  return (regs->ax) ? 0xff00 : 0x0000;
}

static BOOL ifs_deletefile(INTREGS *regs)
{
  COMMAND(IFS_DELETEFILE);
  SPARAM(SDA_FN1);
  iobufptr->data[0]=remote;
  Transmit();
  regs->ax=iobufptr->cmd;
  return (regs->ax) ? 0xff00 : 0x0000;
}

static void NAMEtoFCB(char *name, char fcb[11])
{
  memset(fcb, ' ', 11);
  if (name[0] == '.')
    memcpy(fcb, name, strlen(name));
  else {
    int ii;
    char *ptr;

    for (ii=strlen(name)-1;
         (ii >= 0) &&
         (name[ii] != ':') &&
         (name[ii] != '\\');
         ii--);
    ii++;
    ptr=name+ii;

    for (ii=0; ii < 8; ii++)
      fcb[ii] = (*ptr && (*ptr != '.')) ? *(ptr++) : ' ';
    if (*ptr == '.')
      ptr++;
    for (ii=8; ii < 11; ii++)
      fcb[ii] = (*ptr) ? *(ptr++) : ' ';
  }
}

static BOOL ifs_openfile(INTREGS *regs)
{
  SFT *sft=MK_FP(regs->es, regs->di);
  char *ptr;

  memset(sft, 0, sizeof(*sft));
  sft->handlect=1; /* see ifs_extopen() */
  COMMAND(IFS_OPENFILE);
  *(int *) iobufptr->data = *SDA_OPEN_MODE;
  iobufptr->length=2;
  ptr=iobufptr->data+iobufptr->length;
  SPARAM(SDA_FN1);
  *ptr=remote;
  Transmit();
  regs->ax=iobufptr->cmd;
  if (regs->ax == 0) {
    sft->dir_sector=*(int *) iobufptr->data; /* this is really the file handle */
    sft->open_mode=*SDA_OPEN_MODE;
    sft->attr     =*(int *) (iobufptr->data+2);
    sft->dev_info=0x8040 | (local-'A'+1); /* force drive Z */
    sft->ftime    = *(int *) (iobufptr->data+4);
    sft->fdate    = *(int *) (iobufptr->data+6);
    sft->fsize    = *(long *) (iobufptr->data+8);
    NAMEtoFCB(SDA_FN1, sft->fcb_name);
  }
  return (regs->ax) ? 0xff00 : 0x0000;
}

static BOOL ifs_createfile(INTREGS *regs)
{
  SFT *sft=MK_FP(regs->es, regs->di);
  char *ptr;

  memset(sft, 0, sizeof(*sft));
  sft->handlect=1; /* see ifs_extopen() */
  COMMAND(IFS_CREATEFILE);
  *(int *) iobufptr->data=regs->wparam;
  iobufptr->length=2;
  ptr=iobufptr->data+iobufptr->length;
  SPARAM(SDA_FN1);
  *ptr=remote;
  Transmit();
  sft->handlect=0;
  regs->ax=iobufptr->cmd;
  if (regs->ax == 0) {
    sft->dir_sector=*(int *) iobufptr->data; /* this is really the file handle */
    sft->open_mode=0x02;
    sft->attr     =*(int *) (iobufptr->data+2);
    sft->dev_info=0x8040 | (local-'A'+1);
    sft->ftime    = *(int *) (iobufptr->data+4);
    sft->fdate    = *(int *) (iobufptr->data+6);
    sft->fsize    = *(long *) (iobufptr->data+8);
    NAMEtoFCB(SDA_FN1, sft->fcb_name);
  }
  return (regs->ax) ? 0xff00 : 0x0000;
}

static void DTAtoDIR(struct ffblk *ffblk)
{
  NAMEtoFCB(ffblk->ff_name, SDA_DIR_ENTRY->fname);
  SDA_DIR_ENTRY->attr  = ffblk->ff_attrib;
  SDA_DIR_ENTRY->ftime = ffblk->ff_ftime;
  SDA_DIR_ENTRY->fdate = ffblk->ff_fdate;
  SDA_DIR_ENTRY->cluster=0;
  SDA_DIR_ENTRY->size  = ffblk->ff_fsize;
}

static BOOL ifs_findfirst(INTREGS *regs)
{
  char *ptr;

  COMMAND(IFS_FINDFIRST);
  *(int *) iobufptr->data=*SDA_SRCH_ATTR;
  iobufptr->length=2;
  ptr=iobufptr->data+iobufptr->length;
  SPARAM(SDA_FN1);
  *ptr=remote;

  Transmit();
  regs->ax=iobufptr->cmd;

  {
    struct ffblk *ffblk=(struct ffblk *) iobufptr->data;
    iobufptr->data[0]=(remote-'A'+1);
    memcpy(*SDA_CURR_DTA, ffblk, sizeof(*ffblk));
    memcpy(SDA_SDB, ffblk, sizeof(*SDA_SDB));
    SDA_SDB->drive |= 0x80;
    DTAtoDIR(ffblk);
  }

  return (regs->ax) ? 0xff00 : 0x0000;
}

/*
  packet:
    word = FINDNEXT
  result:
    word = result
*/
static BOOL ifs_findnext(INTREGS *regs)
{
  COMMAND(IFS_FINDNEXT);

  memcpy(iobufptr->data, *SDA_CURR_DTA, sizeof(**SDA_CURR_DTA));
  iobufptr->length=sizeof(**SDA_CURR_DTA);
  iobufptr->data[0] = (remote-'A'+1);
  Transmit();
  regs->ax = iobufptr->cmd;

  {
    struct ffblk *ffblk=(struct ffblk *) iobufptr->data;
    iobufptr->data[0]=(remote-'A'+1);
    memcpy(*SDA_CURR_DTA, ffblk, sizeof(*ffblk));
    memcpy(SDA_SDB, ffblk, sizeof(*SDA_SDB));
    SDA_SDB->drive |= 0x80;
    DTAtoDIR(ffblk);
  }
  return (regs->ax) ? 0xff00 : 0x0000;
}

static BOOL ifs_closeall(INTREGS *regs)
{
  COMMAND(IFS_CLOSEALL);
  iobufptr->length=2;
  Transmit();
  regs->ax=iobufptr->cmd;
  return (regs->ax) ? 0xff00 : 0x0000;
}

static BOOL ifs_seekfromend(INTREGS *regs)
{
  SFT *sft=MK_FP(regs->es, regs->di);
  DWORD nlen=(((DWORD) regs->cx) << 16L) | regs->dx;

  if ((!sft->handlect) || (sft->fsize < nlen)) {
    regs->ax=0x05;
    return 0xff00;
  } else {
    sft->fpos=sft->fsize-nlen;
    regs->ax=sft->fpos & 0xffff;
    regs->dx=sft->fpos >> 16;
    return 0x0000;
  }
}

static BOOL ifs_extopen(INTREGS *regs)
{
  SFT *sft=MK_FP(regs->es, regs->di);
  char *ptr;

  memset(sft, 0, sizeof(*sft));
  sft->handlect=1; /*
                     fake this to `1' because if the server exists
                     on the same machine as the client, this sft
                     NEEDS TO BE RESERVED!
                   */
  COMMAND(IFS_EXTOPEN);
  *(int *) iobufptr->data     = *SDA_EXTOPEN_ACTION;
  *(int *) (iobufptr->data+2) = *SDA_EXTOPEN_MODE;
  *(int *) (iobufptr->data+4) = regs->wparam;
  iobufptr->length=6;
  ptr=iobufptr->data+iobufptr->length;
  SPARAM(SDA_FN1);
  *ptr=remote;
  Transmit();
  sft->handlect=0;
  regs->ax=iobufptr->cmd;
  if (regs->ax == 0) {
    regs->ax=*(int *) (iobufptr->data);   /* file handle  */
    regs->cx=*(int *) (iobufptr->data+2); /* result flags */
    sft->open_mode=*SDA_EXTOPEN_MODE & 0x7f;
    sft->attr     =regs->wparam;
    sft->dev_info=0x8040 | (local-'A'+1); /* force drive Z */
    sft->ftime    = *(int *) (iobufptr->data+4);
    sft->fdate    = *(int *) (iobufptr->data+6);
    sft->fsize    = *(long *) (iobufptr->data+8);
    NAMEtoFCB(SDA_FN1, sft->fcb_name);

    sft->dir_sector=*(int *) iobufptr->data; /* this is really the file handle */
  }
  return (iobufptr->cmd) ? 0xff00 : 0x0000;
}

/*
  chktype = 1-> validate using SDA->FN1
            2-> validate using ES:DI --> SFT
*/
#define IFSNULL {NULL, 0}

static struct {
  BOOL (*fn)(INTREGS *regs);
  char chktype;
} fnTable[] = {
                  IFSNULL,
                  {ifs_rmdir, 1},
                  IFSNULL,
                  {ifs_mkdir, 1},       /* make dir */
                  IFSNULL,
                  {ifs_chdir, 1},       /* change dir */
                  {ifs_closefile, 2},   /* close file */
                  {ifs_commitfile, 2},  /* commit file */
                  {ifs_readfile, 2},    /* read */
                  {ifs_writefile, 2},   /* write */
                  {ifs_lockfile, 2},    /* lock  */
                  {ifs_unlockfile, 2},  /* unlock */
                  {ifs_getspace, 1},    /* get free disk space */
                  IFSNULL,
                  {ifs_setattr, 1},     /* set file attr */
                  {ifs_getattr, 1},     /* get file attr */
                  IFSNULL,
                  {ifs_renamefile, 1},  /* rename file */
                  IFSNULL,
                  {ifs_deletefile, 1},  /* delete file */
                  IFSNULL,
                  IFSNULL,
                  {ifs_openfile, 1},    /* open file */
                  {ifs_createfile, 1},  /* create file */
                  IFSNULL,
                  IFSNULL,
                  IFSNULL,
                  {ifs_findfirst, 1},   /* find first */
                  {ifs_findnext, 1},    /* find next */
                  {ifs_closeall, 1},    /* close all files for a process */
                  IFSNULL,
                  IFSNULL,
                  IFSNULL,
                  {ifs_seekfromend, 1}, /* seek from end of file */
                  {ifs_closeall, 1},    /* process terminate, close all files */
                  IFSNULL,
                  IFSNULL,
                  IFSNULL,
                  IFSNULL,
                  IFSNULL,
                  IFSNULL,
                  IFSNULL,
                  IFSNULL,
                  IFSNULL,
                  IFSNULL,
                  IFSNULL,
                  {ifs_extopen, 1}      /* extended open */
                };
/*
  return
    0xffff: chain
    0x0000: normal return
    0xff00: normal return / set carry
*/

int dispatch(INTREGS *regs)
{
  int fn=regs->ax >> 8,
      subfn=regs->ax & 0xff;
  int ii;

  remote=0;
  regs->flags &= ~1;

  if ((fn == 0x11)
     && (subfn < sizeof(fnTable)/sizeof(*fnTable))
     && fnTable[subfn].fn) {
    switch (fnTable[subfn].chktype) {
      case 1:
        for (ii=0; (ii < CDS_ct); ii++) {
          if (DRIVE_XLAT_TABLE[ii]) {
            CDS *s=(CDS *) (CDS_base+CDS_size*ii);
            if (!strnicmp((*SDA_DRIVE_CDSPTR)->cwd, s->cwd, s->bs)) {
              local='A'+ii;
              remote=DRIVE_XLAT_TABLE[ii];
              break;
            }
          }
        }
        break;
      case 2:
        {
          SFT *sft=MK_FP(regs->es, regs->di);
          for (ii=0; (ii < CDS_ct); ii++) {
            if (DRIVE_XLAT_TABLE[ii]) {
              if ((sft->dev_info & 0x3f) == (ii+1)) {
                local='A'+ii;
                remote=DRIVE_XLAT_TABLE[ii];
                break;
              }
            }
          }
        }
        break;
    }
    if (remote) {
      int ret;
      ret=fnTable[subfn].fn(regs);
      return ret;
    } else
      return 0xffff;
  } else
    return 0xffff;
}

  extern struct {
    BYTE key;
    BYTE attr;
  } *VIDMEM;

/*
  transmit buffer
  wait for response
  if no response in 5 sec, assume trasmission lost, and resend it.
*/
#define CLOCK (*(long *) MK_FP(0x40, 0x6c))

WORD Transmit(void)
{
  int state=0;
  XMITBUF *newbuf = ((iobufptr == iobuf1) ? iobuf2 : iobuf1);
  char *ptr;
  int byte=0, last;
  int retry=5,   /* allow up to 5 retries on send */
      resend=1;
  int len=0;

  iobufptr->packetID[0]='K';
  iobufptr->packetID[1]='Y';
  iobufptr->length+=sizeof(*iobufptr);
  iobufptr->notlength=~iobufptr->length;

  iobufptr->process_id=*SDA_CURR_PSP;
  iobufptr->crc32=0;
  iobufptr->crc32=crc32(0, iobufptr, iobufptr->length);

  do {
    if (resend) {
      CommIO_FlushBuffer();
      CommIO_TransmitLoop(iobufptr, iobufptr->length);
      /* while (CommIO_TransmitPending()); */
      if (retry)
        retry--;
      state=0;
      resend=0;
    }
    last=byte;
    byte=CommIO_WaitByteLoop(73); /* allow 4 second timeout */
    if (byte < 0) {
      resend=1;
      IFS_stat.timeout++;
    } else {
      switch (state) {
        case 0:
          if (byte == 'L')
            state++;
          break;
        case 1:
          if ((last == 'L') && (byte == 'Y')) {
            state++;
            ptr  = (void *) newbuf;
            *(ptr++)=last;
            *(ptr++)=byte;
            len=2;
          } else
            resend=1;
          break;
        case 2:
          *(ptr++)=byte;
          len++;
          if ((len == 6) && (newbuf->length != ~newbuf->notlength)) {
            resend=1;
            IFS_stat.lenfail++;
          } else if (len == newbuf->length) {
            DWORD oldcrc=newbuf->crc32;
            newbuf->crc32=0;
            if (crc32(0, newbuf, newbuf->length) != oldcrc) {
              resend=1;
              IFS_stat.crcfail++;
            } else
              retry=-1; /* flag that we've completed */
          }
      }
    }
  } while (retry > 0);
  CommIO_FlushBuffer();
  iobufptr=newbuf;
  if (retry == 0)
    iobufptr->cmd=0x15;
  else
    IFS_stat.valid += iobufptr->length;
  return iobufptr->cmd;
}

static char *options[]={"/speed=",
               "/com1",
               "/com2",
               "/com3",
               "/com4",
               "/irq=",
               "/port=",
               "/remove",
               "/intno=",
               NULL};

void main(int argc, char **argv)
{
  char *ptr;
  long  speed=19200;
  int   com=-1,
        irq=-1,
        port=-1,
        intno=FindUnusedInt();
  BOOL  removeflag=FALSE;
  int   opt;
  int   RIFS=FindRIFS();

  if ((_osmajor != 0x03) && (_osmajor < 0x05)) {
    fprintf(stderr, "RIFS Client Module is only compatable with DOS 3.x,\n"
                    "and DOS >= 5.x (not DOS 4.x).\n");
    exit(1);
  }
  {
    /*
      1st lose the environment
    */
    unsigned seg=*(WORD *) MK_FP(_psp, 0x2c);
    freemem(seg);
  }
  /*
    init variables
  */
  ArgInit(argc, argv);

  while ((opt=GetOption(options)) != -1) {
    switch (opt) {
      case 0:
        speed=strtol(GetArg(), &ptr, 0x0a);
        if (speed == 0)
          ArgError("speed out of range [2..115200]");
        if (115200L % speed)
          speed=115200/(115200/speed+1);
        break;
      case 1:
      case 2:
      case 3:
      case 4:
        com=opt;
        break;
      case 5:
        irq=strtol(GetArg(), &ptr, 0x10);
        if ((irq < 0) || (irq > 0x0f))
          ArgError("IRQ out of range [0..0f]");
        break;
      case 6:
        port=strtol(GetArg(), &ptr, 0x10);
        break;
      case 7:
        removeflag=1;
        break;
      case 8:
        intno=strtol(GetArg(), &ptr, 0x10);
        break;
    }
  }

  if (removeflag) {
    struct REGPACK regs;

    if (RIFS) {
      regs.r_ax=0;
      intr(RIFS, &regs);
    }
    if (!RIFS || (regs.r_ax != 0x1234)) {
      printf("RCLIENT not found\n");
      exit(1);
    } else {
      regs.r_ax=1;
      intr(RIFS, &regs);
      printf("RCLIENT removed from memory\n");
      exit(0);
    }
  } else if (RIFS) {
    struct REGPACK regs;
    regs.r_ax=RCLIENT_QUERY;
    intr(RIFS, &regs);
    if (regs.r_ax == 0x1234) {
      printf("RCLIENT already loaded\n");
      exit(1);
    }
    regs.r_ax=RSERVER_QUERY;
    intr(RIFS, &regs);
    if (regs.r_ax == 0x4321) {
      printf("RSERVER is loaded\n");
      exit(1);
    }
  }
  if ((com < 0) && ((irq < 0) || (port < 0))) {
    for (com=1; com <= 4; com++)
      if (*(WORD *) (MK_FP(0x40, 2*(com-1))))
        break;
    if (com > 4)
      ArgError("cannot find available com port");
  }
  if (!intno)
    ArgError("cannot find unused user interrupt");
  InitClient(intno);
  CommIO_Initialize(com, STOP_1 | WORD_8 | PARITY_NONE, speed, 0, port);
  printf("Client initialized on port (%d) at (%ld)\n", com, speed);
  {
    MCB *mcb=MK_FP(_psp-1, 0);
    keep(0, mcb->size);
  }
}

/*
  map local drive [local] ('A'..'Z') to remote drive [remote] ('A'..'Z')
*/
BOOL RemapDrive(BYTE local, BYTE remote)
{
  CDS *cds;

  if (isalpha(remote) && isalpha(local)) {
    local=toupper(local)-'A';
    remote=toupper(remote);
    DRIVE_XLAT_TABLE[local]=remote;
    cds=(CDS *) (CDS_base + local * CDS_size);
    memset(cds, 0, CDS_size);
    strcpy(cds->cwd, " :\\");
    cds->cwd[0]=local+'A';
    cds->flags = NETWORK | PHYSICAL;
    cds->bs    = strlen(cds->cwd)-1;
    return TRUE;
  } else
    return FALSE;
}

/*
  return drive map to original (startup)
*/
BOOL UnmapDrive(BYTE dv)
{
  CDS *origcds,
      *cds;

  if (isalpha(dv)) {
    dv=toupper(dv)-'A';
    DRIVE_XLAT_TABLE[dv]=0;
    cds     =(CDS *) (CDS_base + dv * CDS_size);
    origcds =(CDS *) (CDS_init + dv * CDS_size);
    memcpy(cds, origcds, CDS_size);
    return TRUE;
  }
  return FALSE;
}

BOOL UnmapAll(void)
{
  memcpy(CDS_base, CDS_init, CDS_size);
  return TRUE;
}

void UserInt(INTREGS *regs)
{
  /*
    RCLIENT functions...
  */
  regs->flags &= ~1;
  switch (regs->ax) {
    case RCLIENT_QUERY: /* query client / server */
      regs->ax=0x1234;
      break;
    case RCLIENT_UNLOAD: /* unload */
      uninit();
      freemem(_psp);
      break;
    case RCLIENT_REMAP: /* remap (local BH == remote BL) */
      regs->ax=RemapDrive(regs->bx >> 8, regs->bx & 0xff);
      break;
    case RCLIENT_UNMAP: /* unmap BL */
      regs->ax=UnmapDrive(regs->bx & 0xff);
      break;
    case RCLIENT_UNMAPALL: /* unmap ALL */
      regs->ax=UnmapAll();
      break;
    case RCLIENT_GETXLAT: /* return translation table */
      regs->es=FP_SEG(DRIVE_XLAT_TABLE);
      regs->bx=FP_OFF(DRIVE_XLAT_TABLE);
      break;
    case RCLIENT_GETSTAT:
      regs->es=FP_SEG(&IFS_stat);
      regs->bx=FP_OFF(&IFS_stat);
      IFS_stat.totalsent=total_sent;
      IFS_stat.totalrcvd=total_rcvd;
      IFS_stat.stackused=ifs0_GetStackUsed();
      break;
    default:
      regs->flags |= 1;
  }
}
