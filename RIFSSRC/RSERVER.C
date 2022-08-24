/*
  SERVER
    1. wait for request packets
    2. execute request packet
    3. package result
    4. continue

  notes:
     each function is in C but calls DOS through intr().
     this is clunky, i know, but it comes from a bizarre design. initially
     i thought i could translate everything into C calls (open, close, etc..)
     but no such luck because i had to translate the C return codes to DOS
     codes which was much too much trouble. later, it turns out the that
     _doserrno holds the DOS return code, but i had already re-worked
     my code.

     this is almost entirely CLIENT controlled. the server makes few
     decisions. all filenames are translated at the client, etc...
*/
#include <stdio.h>
#include <stdlib.h>
#include <dos.h>
#include <dir.h>
#include <string.h>
#include <assert.h>
#include <io.h>
#include <fcntl.h>
#include <sys\stat.h>
#include <errno.h>
#include <conio.h>
#include <time.h>

#include "ifs.h"
#include "comio.h"
#include "crc32.h"
#include "svr0.h"
#include "myalloc.h"
#include "rifs.h"

#include "rclient.h"

unsigned _stklen = 512;      /* minimal local stack */

BYTE    *SDA;           /* swappable data area         */
WORD     SDA_maxsize;   /* size of SDA                 */
WORD     SDA_minsize;   /* min. size of SDA            */
BYTE    *SDA_DOSBUSY;   /* DOS busy flag               */
BYTE    *CDS_base;      /* current directory structure */
WORD     CDS_EntrySize; /* # of bytes per entry        */
WORD     CDS_ct;        /* number of entries           */
WORD     CDS_TotalSize; /* total number of bytes       */

/*************************************************************************
 *************************************************************************
  SERVER
 *************************************************************************
 *************************************************************************/
XMITBUF *iobuf;    /* send / recieve buffer(s)    */
XMITBUF *iobufptr; /* pointer to recieve buffer */

BYTE *lcl_SDA;  /* local copy of SDA    */
CDS  *lcl_CDS;  /* local copy of CDS    */

static int   datasize = BLOCKSIZE; /* size of packet data  */

static int   state;    /*
                          used by ASYNC for state info:
                          0 = looking for 'K'
                          1 = looking for 'Y'
                          2 = storing ALL incoming
                          3 = waiting to finish server request
                       */
static IFS_STAT IFS_stat; /* struct for keeping statistics */
BOOL background=TRUE; /* TRUE if running in background, else FALSE */

static unsigned openpsp[MAXOPEN];

extern struct {
  BYTE key;
  BYTE attr;
} *VIDMEM;

#define SETRESULT(a) {iobufptr->length=0; iobufptr->cmd=a;}

/*
  remove directory
    ASCIIz = name of directory to remove
*/
static void svr_rmdir(XMITBUF *iobuf)
{
  struct REGPACK regs;

  regs.r_ax=0x3a00;
  regs.r_dx=FP_OFF(iobuf->data);
  regs.r_ds=FP_SEG(iobuf->data);
  intr(0x21, &regs);
  if (regs.r_flags & 0x01) {
    SETRESULT(regs.r_ax);
  } else {
    SETRESULT(0);
  }
}

/*
  make directory
    ASCIIz = name of directory to create
*/
static void svr_mkdir(XMITBUF *iobuf)
{
  struct REGPACK regs;

  regs.r_ax=0x3900;
  regs.r_dx=FP_OFF(iobuf->data);
  regs.r_ds=FP_SEG(iobuf->data);
  intr(0x21, &regs);
  if (regs.r_flags & 0x01) {
    SETRESULT(regs.r_ax);
  } else {
    SETRESULT(0);
  }
}
/*
  change directory
    ASCIIz = name of directory to change
*/
static void svr_chdir(XMITBUF *iobuf)
{
  struct REGPACK regs;

  regs.r_ax=0x3b00;
  regs.r_dx=FP_OFF(iobuf->data);
  regs.r_ds=FP_SEG(iobuf->data);
  intr(0x21, &regs);
  if (regs.r_flags & 0x01) {
    SETRESULT(regs.r_ax);
  } else {
    SETRESULT(0);
  }
}

/*
  close file
    WORD : handle of file to close
*/
static void svr_closefile(XMITBUF *iobuf)
{
  struct REGPACK regs;

  regs.r_ax=0x3e00;
  regs.r_bx=*(int *) iobuf->data;
  if (regs.r_bx < MAXOPEN)
    openpsp[regs.r_bx]=0;
  intr(0x21, &regs);

  if (regs.r_flags & 0x01) {
    SETRESULT(regs.r_ax);
  } else {
    SETRESULT(0);
  }
}

/*
  commit file buffers
    WORD : handle of file to commit
*/
static void svr_commitfile(XMITBUF *iobuf)
{
  SETRESULT(0);
}

/*
  read from a file
    WORD  : handle
    DWORD : position
    WORD  : length
  returns
    WORD   : number of bytes read
    BYTE[] : bytes
*/
static void svr_readfile(XMITBUF *iobuf)
{
  int  handle=*(int *) iobuf->data;
  long pos=   *(long *) (iobuf->data+2);
  int  len=   *(int *) (iobuf->data+6);
  struct REGPACK regs;

  regs.r_ax=0x4200;             /* 1st seek to position */
  regs.r_bx=handle;
  regs.r_cx=(pos >> 16);
  regs.r_dx=(pos & 0xffff);
  intr(0x21, &regs);
  if (regs.r_flags & 0x01) {
    SETRESULT(regs.r_ax);
    *(int *) iobuf->data=0;
    iobuf->length=2;
  } else {
    regs.r_ax=0x3f00;           /* next read from file */
    regs.r_bx=handle;
    regs.r_cx=len;
    regs.r_dx=FP_OFF(iobuf->data)+2;
    regs.r_ds=FP_SEG(iobuf->data);
    intr(0x21, &regs);
    if (regs.r_flags & 0x01) {
      SETRESULT(regs.r_ax);
      *(int *) iobuf->data=0;
      iobuf->length=2;
    } else {
      SETRESULT(0);
      *(int *) iobuf->data=regs.r_ax;
      iobuf->length=2+regs.r_ax;
    }
  }
}

/*
  write to file
    WORD    : file handle
    DWORD   : position
    WORD    : length
    BYTES[] : data
  return
    WORD    : bytes written, 0xffff = error
*/
static void svr_writefile(XMITBUF *iobuf)
{
  int  handle=*(int *) iobuf->data;
  long pos=   *(long *) (iobuf->data+2);
  int  len=   *(int *) (iobuf->data+6);
  struct REGPACK regs;

  regs.r_ax=0x4200;
  regs.r_bx=handle;
  regs.r_cx=(pos >> 16);
  regs.r_dx=(pos & 0xffff);
  intr(0x21, &regs);
  if (regs.r_flags & 0x01) {
    SETRESULT(regs.r_ax);
    *(int *) iobuf->data=0;
  } else {
    regs.r_ax=0x4000;
    regs.r_bx=handle;
    regs.r_cx=len;
    regs.r_dx=FP_OFF(iobuf->data)+8;
    regs.r_ds=FP_SEG(iobuf->data);
    intr(0x21, &regs);
    if (regs.r_flags & 0x01) {
      SETRESULT(regs.r_ax);
      *(int *) iobuf->data=0;
    } else {
      SETRESULT(0);
      *(int *) iobuf->data=regs.r_ax;
    }
  }
  iobuf->length=2;
}

/*
  lock file bytes
    WORD  : handle
    DWORD : position
    DWORD : length
*/
static void svr_lockfile(XMITBUF *iobuf)
{
  struct REGPACK regs;

  int handle=*(int *) iobuf->data;
  long pos = *(long *) (iobuf->data + 2);
  long len = *(long *) (iobuf->data + 6);

  regs.r_ax=0x5c00;
  regs.r_bx=handle;
  regs.r_cx=(pos >> 16);
  regs.r_dx=pos & 0xffff;
  regs.r_si=(len >> 16);
  regs.r_di=len & 0xffff;
  intr(0x21, &regs);
  if (regs.r_flags & 0x01) {
    SETRESULT(regs.r_ax);
  } else {
    SETRESULT(0);
  }
}

/*
  lock/unlock file bytes
    WORD  : handle
    WORD  : function (0 = lock / 1 = unlock)
    DWORD : position
    DWORD : size
*/
static void svr_unlockfile(XMITBUF *iobuf)
{
  int handle=*(int *)   iobuf->data;
  int fn    =*(int *)  (iobuf->data + 2);
  long pos = *(long *) (iobuf->data + 4);
  long len = *(long *) (iobuf->data + 8);
  struct REGPACK regs;

  regs.r_ax=0x5c00 + (fn ? 0 : 1);
  regs.r_bx=handle;
  regs.r_cx=(pos >> 16);
  regs.r_dx=pos & 0xffff;
  regs.r_si=(len >> 16);
  regs.r_di=len & 0xffff;
  intr(0x21, &regs);
  if (regs.r_flags & 0x01) {
    SETRESULT(regs.r_ax);
  } else {
    SETRESULT(0);
  }
}

/*
  get free space
    BYTE : drive number (0 = a)
  return
    struct dfree
*/
static void svr_getspace(XMITBUF *iobuf)
{
  struct dfree *df=(void *) iobuf->data;
  getdfree(iobuf->data[0], df);
  if (df->df_sclus == 0xffff) {
    SETRESULT(errno);
  } else {
    SETRESULT(0);
  }
  iobuf->length = sizeof(*df);
}

/*
  set file attribute
    WORD   : attribute
    ASCIIz : filename
*/
static void svr_setattr(XMITBUF *iobuf)
{
  struct REGPACK regs;

  regs.r_ax=0x4301;
  regs.r_cx=*(int *) iobuf->data;
  regs.r_dx=FP_OFF(iobuf->data)+2;
  regs.r_ds=FP_SEG(iobuf->data);
  intr(0x21, &regs);
  if (regs.r_flags & 0x01) {
    SETRESULT(regs.r_ax);
  } else
    SETRESULT(0);
}

/*
  return file attribute
    ASCIIz : filename
  return
    WORD   : attribute
*/
static void svr_getattr(XMITBUF *iobuf)
{
  struct REGPACK regs;

  regs.r_ax=0x4300;
  regs.r_dx=FP_OFF(iobuf->data);
  regs.r_ds=FP_SEG(iobuf->data);
  intr(0x21, &regs);
  if (regs.r_flags & 0x01) {
    SETRESULT(regs.r_ax);
  } else {
    SETRESULT(0);
    *(int *) iobuf->data=regs.r_cx;
    iobuf->length=2;
  }
}

/*
  rename file
    ASCIIz : old name
    ASCIIz : new name
*/
static void svr_renamefile(XMITBUF *iobuf)
{
  struct REGPACK regs;

  regs.r_ax=0x5600;
  regs.r_dx=FP_OFF(iobuf->data);
  regs.r_di=FP_OFF(iobuf->data)+strlen(iobuf->data)+1;
  regs.r_ds=regs.r_es=FP_SEG(iobuf->data);
  intr(0x21, &regs);
  if (regs.r_flags & 0x01) {
    SETRESULT(regs.r_ax);
  } else
    SETRESULT(0);
}

/*
  delete file
    ASCIIz : filename
*/
static void svr_deletefile(XMITBUF *iobuf)
{
  struct REGPACK regs;

  regs.r_ax=0x4100;
  regs.r_dx=FP_OFF(iobuf->data);
  regs.r_ds=FP_SEG(iobuf->data);
  intr(0x21, &regs);
  if (regs.r_flags & 0x01) {
    SETRESULT(regs.r_ax);
  } else
    SETRESULT(0);
}

/*
  open file
    WORD   : open mode
    ASCIIz : filename
  return
    WORD   : file handle
    WORD   : file attribute
    WORD   : file time
    WORD   : file date
    DWORD  : file length
*/
static void svr_openfile(XMITBUF *iobuf)
{
  struct REGPACK regs;

  /*
    call DOS open
  */
  regs.r_ax=0x3d00 | ((*(int *) iobuf->data) & 0xff);
  regs.r_dx=FP_OFF(iobuf->data)+2;
  regs.r_ds=FP_SEG(iobuf->data);
  intr(0x21, &regs);
  if (regs.r_flags & 0x01) {
    SETRESULT(regs.r_ax);
  } else {
    SETRESULT(0);
    if (regs.r_ax < MAXOPEN)
      openpsp[regs.r_ax]=iobuf->process_id;
    *(int *) iobuf->data=regs.r_ax;
    *(int *) (iobuf->data+2)=_chmod(iobuf->data+2, 0);
    regs.r_bx=*(int *) iobuf->data;   /* get file date/time      */
    regs.r_ax=0x5700;                 /* assume this never fails */
    intr(0x21, &regs);
    *(int *) (iobuf->data+ 4) = regs.r_cx;
    *(int *) (iobuf->data+ 6) = regs.r_dx;
    regs.r_bx=*(int *) iobuf->data;   /* get file size           */
    regs.r_cx=regs.r_dx=0;            /* assume this never fails */
    regs.r_ax=0x4202;
    intr(0x21, &regs);
    if (regs.r_flags & 0x01)
      regs.r_dx=regs.r_ax=0;
    *(int *) (iobuf->data+ 8) = regs.r_ax;
    *(int *) (iobuf->data+10) = regs.r_dx;
    iobuf->length = 12;
  }
}

/*
  create file
    WORD   : create mode
    ASCIIz : filename
  return
    WORD   : file handle
    WORD   : file attribute
    WORD   : file time
    WORD   : file date
    DWORD  : file length
*/
static void svr_createfile(XMITBUF *iobuf)
{
  struct REGPACK regs;

  regs.r_ax=0x3c00;
  regs.r_cx=*(int *) iobuf->data;
  regs.r_dx=FP_OFF(iobuf->data)+2;
  regs.r_ds=FP_SEG(iobuf->data);
  intr(0x21, &regs);
  if (regs.r_flags & 0x01) {
    SETRESULT(regs.r_ax);
  } else {
    SETRESULT(0);
    if (regs.r_ax < MAXOPEN)
      openpsp[iobuf->process_id]=regs.r_ax;
    *(int *) iobuf->data=regs.r_ax;
    *(int *) (iobuf->data+2)=_chmod(iobuf->data+2, 0);
    regs.r_bx=*(int *) iobuf->data;   /* get file date/time      */
    regs.r_ax=0x5700;                 /* assume this never fails */
    intr(0x21, &regs);
    *(int *) (iobuf->data+ 4) = regs.r_cx;
    *(int *) (iobuf->data+ 6) = regs.r_dx;
    regs.r_bx=*(int *) iobuf->data;   /* get file size           */
    regs.r_cx=regs.r_dx=0;            /* assume this never fails */
    regs.r_ax=0x4202;
    intr(0x21, &regs);
    if (regs.r_flags & 0x01)
      regs.r_dx=regs.r_ax=0;
    *(int *) (iobuf->data+ 8) = regs.r_ax;
    *(int *) (iobuf->data+10) = regs.r_dx;
    iobuf->length = 12;
  }
}

/*
  find first data block
    WORD   : attrib
    ASCIIz : path
  result:
    struct ffblk
*/
static void svr_findfirst(XMITBUF *iobuf)
{
  struct REGPACK regs;
  void *oldDTA;
  char buf[43];

  regs.r_ax=0x2f00;                     /* get old DTA */
  intr(0x21, &regs);
  oldDTA=MK_FP(regs.r_es, regs.r_bx);

  regs.r_ax=0x1a00;                    /* set new DTA */
  regs.r_ds=FP_SEG(buf);
  regs.r_dx=FP_OFF(buf);
  intr(0x21, &regs);

  regs.r_ax=0x4e00;
  regs.r_cx=*(int *) (iobuf->data);
  regs.r_dx=FP_OFF(iobuf->data)+2;
  regs.r_ds=FP_SEG(iobuf->data);
  intr(0x21, &regs);

  if (regs.r_flags & 0x01) {
    SETRESULT(regs.r_ax);
  } else {
    SETRESULT(0);
  }
  memcpy(iobuf->data, buf, 43);
  iobuf->length=43;

  regs.r_ax=0x1a00;
  regs.r_ds=FP_SEG(oldDTA);
  regs.r_dx=FP_OFF(oldDTA);
  intr(0x21, &regs);
}

/*
  find next data block
    struct ffblk ffblk (from findfirst())
  result:
    struct ffblk
*/
static void svr_findnext(XMITBUF *iobuf)
{
  struct REGPACK regs;
  void *oldDTA;

  regs.r_ax=0x2f00;                     /* get old DTA */
  intr(0x21, &regs);
  oldDTA=MK_FP(regs.r_es, regs.r_bx);

  regs.r_ax=0x1a00;                    /* set new DTA */
  regs.r_ds=FP_SEG(iobuf->data);
  regs.r_dx=FP_OFF(iobuf->data);
  intr(0x21, &regs);

  regs.r_ax=0x4f00;
  regs.r_dx=FP_OFF(iobuf->data);
  regs.r_ds=FP_SEG(iobuf->data);
  intr(0x21, &regs);

  if (regs.r_flags & 0x01) {
    SETRESULT(regs.r_ax);
  } else {
    SETRESULT(0);
  }
  iobuf->length=43;

  regs.r_ax=0x1a00;
  regs.r_ds=FP_SEG(oldDTA);
  regs.r_dx=FP_OFF(oldDTA);
  intr(0x21, &regs);
}

void CloseAll(unsigned psp)
{
  int ii;

  for (ii=0; ii < MAXOPEN; ii++)
    if (openpsp[ii] && (!psp || (openpsp[ii] == psp))) {
      openpsp[ii]=0;
      close(ii);
    }
}

static void svr_closeall(XMITBUF *iobuf)
{
  CloseAll(iobuf->process_id);
  SETRESULT(0);
}

/*
  extended file open
    WORD   : action
    WORD   : open mode
    WORD   : create attribute
    ASCIIz : filename
  return
    WORD   : handle
    WORD   : status (0 = opened, 1 = created, 2 = replaced)
    WORD   : file time
    WORD   : file date
    DWORD  : file length
*/
static void svr_extopen(XMITBUF *iobuf)
{
  struct REGPACK regs;

  regs.r_ax=0x6c00;
  regs.r_bx=*(int *) (iobuf->data+2);
  regs.r_cx=*(int *) (iobuf->data+4) & 0xff;
  regs.r_dx=*(int *) (iobuf->data);
  regs.r_ds=FP_SEG(iobuf->data);
  regs.r_si=FP_OFF(iobuf->data)+6;
  intr(0x21, &regs);
  if (regs.r_flags & 0x01) {
    SETRESULT(regs.r_ax);
  } else {
    SETRESULT(0);
    if (regs.r_ax < MAXOPEN)
      openpsp[regs.r_ax]=iobuf->process_id;
    *(int *) iobuf->data=regs.r_ax;
    *(int *) (iobuf->data+2) = regs.r_cx;
    regs.r_bx=*(int *) iobuf->data;   /* get file date/time      */
    regs.r_ax=0x5700;                 /* assume this never fails */
    intr(0x21, &regs);
    *(int *) (iobuf->data+ 4) = regs.r_cx;
    *(int *) (iobuf->data+ 6) = regs.r_dx;
    regs.r_bx=*(int *) iobuf->data;   /* get file size           */
    regs.r_cx=regs.r_dx=0;            /* assume this never fails */
    regs.r_ax=0x4202;
    intr(0x21, &regs);
    if (regs.r_flags & 0x01)
      regs.r_dx=regs.r_ax=0;
    *(int *) (iobuf->data+ 8) = regs.r_ax;
    *(int *) (iobuf->data+10) = regs.r_dx;
    iobuf->length = 12;
  }
}

static struct {
  void (*fn)(XMITBUF *iobuf);
} svrTable[] = {
                  svr_rmdir,
                  svr_mkdir,
                  svr_chdir,
                  svr_closefile,
                  svr_commitfile,
                  svr_readfile,
                  svr_writefile,
                  svr_lockfile,
                  svr_unlockfile,
                  svr_getspace,
                  svr_setattr,
                  svr_getattr,
                  svr_renamefile,
                  svr_deletefile,
                  svr_openfile,
                  svr_createfile,
                  svr_findfirst,
                  svr_findnext,
                  svr_closeall,
                  svr_extopen
                };

/*
  shutdown server and free memory
*/
static void uninit(void)
{
  if (lcl_SDA) {
    svr0_shutdown();
    my_free(lcl_SDA);
    my_free(lcl_CDS);
    my_free(iobuf);
    lcl_SDA=0;
  }
}

/*
  return 0 if OK, else fail code
*/
int InitServer(int intno)
{
  struct REGPACK regs;
  BYTE *LOL; /* list of lists       */

  regs.r_ax=0x5d06;
  intr(0x21, &regs);
  SDA=MK_FP(regs.r_ds, regs.r_si);
  SDA_maxsize=regs.r_cx;
  SDA_minsize=regs.r_dx;
  SDA_DOSBUSY = (void *) (SDA+1);

  /*
    init lcl_SDA (for SERVER)
  */
  regs.r_ax=0x5200;
  intr(0x21, &regs);
  LOL=MK_FP(regs.r_es, regs.r_bx);

  CDS_base=*(void **) (LOL+0x16);
  CDS_ct=*(BYTE *) (LOL + 0x21);
  CDS_EntrySize=(_osmajor == 0x03) ? 0x51 : 0x58;
  CDS_TotalSize=CDS_ct * CDS_EntrySize;

  if (background) {
    lcl_SDA=my_malloc(SDA_maxsize);
    if (!lcl_SDA)
      return 1;
    memcpy(lcl_SDA, SDA, SDA_maxsize);
    lcl_CDS=my_malloc(CDS_TotalSize);
    if (!lcl_CDS) {
      my_free(lcl_SDA);
      return 1;
    }
    memcpy(lcl_CDS, CDS_base, CDS_TotalSize);
  }

  iobufptr=iobuf=my_malloc(sizeof(*iobuf)+datasize);

  if (!iobuf) {
    if (lcl_SDA)
      my_free(lcl_SDA);
    if (lcl_CDS)
      my_free(lcl_CDS);
    if (iobuf)
      my_free(iobuf);
    return 1;
  }
  svr0_init(intno);
  return 0;
}

static long dispatchct=0;
static int  dispatchfn=0;

void ServerDispatch(void)
{
  void interrupt (*oldint1b)();
  void interrupt (*oldint23)();
  void interrupt (*oldint24)();

  dispatchct++;

  oldint1b=getvect(0x1b);
  setvect(0x1b, svr0_int1b);

  oldint23=getvect(0x23);     /* swap ctl-break address */
  setvect(0x23, svr0_int23);  /* (ignore ctl-break) */

  oldint24=getvect(0x24);     /* swap critical error question */
  setvect(0x24, svr0_int24);  /* return FAIL always */

  IFS_stat.inserver++;
  if ((iobufptr->cmd >= 0) &&            /* check for valid function    */
      (iobufptr->cmd < IFS_ENDOFLIST) &&
       svrTable[iobufptr->cmd].fn) {
    dispatchfn=iobufptr->cmd;
    svrTable[iobufptr->cmd].fn(iobufptr);        /* call if valid */
  } else {
    SETRESULT(0x05);                  /* otherwise result is invalid */
  }

  iobufptr->packetID[0]='L';
  iobufptr->packetID[1]='Y';
  iobufptr->length += sizeof(*iobufptr);
  iobufptr->notlength = ~iobufptr->length;
  iobufptr->crc32 = 0;
  iobufptr->crc32=crc32(0, iobufptr, iobufptr->length);
  if (background)
    CommIO_Transmit(iobufptr, iobufptr->length);
  else
    CommIO_TransmitLoop(iobufptr, iobufptr->length);
  setvect(0x1b, oldint1b);
  setvect(0x23, oldint23);
  setvect(0x24, oldint24);
  svr0_ResetDispatchFlag();           /* tell the world         */
  state=0;                            /* ready for next request */
}

void _Recieve(void)
{
  static char *ptr=0;
  static int   rcvd=0;
  static int   last=0;
  int key;

  if (!background)
    key=CommIO_WaitByteLoop(36);
  else
    key=-1;
  while ((state != 3) && (CommIO_RecievePending() || (key >= 0))) {

    if (background)
      key=CommIO_GetByte();

    switch (state) {
      case 0: /* no current state, wait for 'K' */
        if (key == 'K')
          state++;
        break;
      case 1: /* 'K' found, if next character is 'Y', increase state */
        if ((last == 'K') && (key == 'Y')) {
          ptr=(char *) iobufptr;
          iobufptr->length=0;
          *(ptr++)=last;
          *(ptr++)=key;
          rcvd=2;
          state++;
        } else /* otherwise reset state */
          state=0;
        break;
      case 2:
        *(ptr++)=key;
        if (++rcvd == sizeof(*iobufptr)) {
          if ((iobufptr->length != ~iobufptr->notlength)
               || (iobufptr->length > datasize+sizeof(*iobufptr))) {
            CommIO_FlushBuffer();
            state=0;
            IFS_stat.lenfail++;
          }
        }
        if (rcvd == iobufptr->length) {
          DWORD ocrc=iobufptr->crc32;
          iobufptr->crc32=0;
          if (crc32(0, iobufptr, iobufptr->length) != ocrc) {
            CommIO_FlushBuffer();
            IFS_stat.crcfail++;
            state=0;
          } else {
            IFS_stat.valid += iobufptr->length;
            state++;
            svr0_SetDispatchFlag();
          }
        }
    }
    last=key;
    if (!background && (state != 3))
      key=CommIO_WaitByteLoop(36);
    else
      key=-1;
  }
}

static char *options[]={"/speed=",
               "/com1",
               "/com2",
               "/com3",
               "/com4",
               "/irq=",
               "/port=",
               "/remove",
               "/nobackground",
               "/intno=",
               "/reset",
               NULL};

void main(int argc, char **argv)
{
  char *ptr;
  long  speed=19200;
  int   com=-1,
        irq=-1,
        port=-1,
        intno=FindUnusedInt();
  BOOL  removeflag=FALSE,
        resetflag=FALSE;
  int   opt, RIFS=FindRIFS();
  {
    /*
      1st lose the environment
    */
    unsigned seg=*(WORD *) MK_FP(_psp, 0x2c);
    freemem(seg);
  }
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
        removeflag=TRUE;
        break;
      case 8:
        background=FALSE;
        break;
      case 9:
        intno=strtol(GetArg(), &ptr, 0x10);
        break;
      case 10:
        resetflag=TRUE;
        break;
    }
  }
  if (removeflag || resetflag) {
    struct REGPACK regs;

    if (RIFS) {
      regs.r_ax=RSERVER_QUERY;
      intr(RIFS, &regs);
    }
    if (!RIFS || (regs.r_ax != 0x4321)) {
      printf("RSERVER not found\n");
      exit(1);
    } else {
      regs.r_ax=(removeflag) ? RSERVER_UNLOAD : RSERVER_RESET;
      intr(RIFS, &regs);
      printf("RSERVER %s\n", removeflag ? "removed from memory" : "reset");
      exit(0);
    }
  } else if (RIFS) {
    struct REGPACK regs;
    regs.r_ax=RSERVER_QUERY;
    intr(RIFS, &regs);
    if (regs.r_ax == 0x4321) {
      printf("RSERVER already loaded\n");
      exit(1);
    }
    regs.r_ax=RCLIENT_QUERY;
    intr(RIFS, &regs);
    if (regs.r_ax == 0x1234) {
      printf("RCLIENT already loaded\n");
      exit(1);
    }
  }
  if ((com < 0) && ((irq < 0) || (port < 0))) {
    for (com=0; com < 4; com++)
      if (*(WORD *) (MK_FP(0x40, 2*com)))
        break;
    if (com == 4)
      ArgError("cannot find available com port");
    else
      com++;
  }
  if (!intno)
    ArgError("cannot find unused user interrupt");
  if (background && ((_osmajor != 0x03) && (_osmajor < 0x05))) {
    fprintf(stderr, "RIFS Server Background Mode is ONLY compatable with\n"
                    "DOS 3.x and DOS >= 5.x (not 4.x)\n");
    exit(1);
  }

  {
    int res=InitServer(intno);
    if (res) {
      fprintf(stderr, "Error (%d) initializing server\n", res);
      exit(1);
    }
    res = CommIO_Initialize(com, STOP_1 | WORD_8 | PARITY_NONE, speed, background ? irq : 0, port);
    if (res) {
      fprintf(stderr, "Error (%d) initializing serial port\n");
      uninit();
      exit(1);
    }
  }
  printf("IFS server initialized on port (%d) at (%ld)\n", com, speed);
  if (background) {
    MCB *mcb=MK_FP(_psp-1, 0);
    keep(0, mcb->size);
  } else {
    for ( ; ; ) {
      _Recieve();
      if (state == 3)
        ServerDispatch();
      if (kbhit() && getch()  == 27)
        break;
      putch('*');
    }
  }
  CommIO_shutdown();
  CloseAll(0);
  uninit();
}

/*
  user interrupt for communicating with the server
*/
void UserInt(INTREGS *regs)
{
  regs->flags &= ~0x01;
  switch (regs->ax) {
    case RSERVER_QUERY:
      regs->ax=0x4321;
      break;
    case RSERVER_GETSTAT:
      regs->es=FP_SEG(&IFS_stat);
      regs->bx=FP_OFF(&IFS_stat);
      IFS_stat.totalsent=total_sent;
      IFS_stat.totalrcvd=total_rcvd;
      IFS_stat.stackused=svr0_GetStackUsed();
      IFS_stat.openpsp=openpsp;
      IFS_stat.openhandle=MK_FP(_psp, 0x18);
      break;
    case RSERVER_UNLOAD:
      SwapDOS();
      CloseAll(0);
      SwapDOS();
      CommIO_shutdown();
      uninit();
      freemem(_psp);
      break;
    case RSERVER_RESET: /* close all open files */
      SwapDOS();
      CloseAll(0);
      SwapDOS();
      break;
    default:
      regs->flags |= 0x01;
  }
}
