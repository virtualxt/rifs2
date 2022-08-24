#ifndef ifs_h
#define ifs_h

#define IFSBAUD 19200

typedef enum { FALSE, TRUE } BOOL;

#define NETWORK    (1U << 15)
#define PHYSICAL   (1U << 14)
#define JOIN       (1U << 13)
#define SUBST      (1U << 12)

#define BYTE  unsigned char
#define WORD  unsigned
#define DWORD unsigned long

typedef struct {
  BYTE    cwd[67];
  WORD    flags;
  void   *DBP;
  DWORD   redirifs;
  WORD    junk1;
  WORD    bs;       /* pos of root backslash (normally 2) */
} CDS;

typedef struct {
  BYTE  srchattr;     /* attribute to search for    */
  BYTE  drive;        /* drive number (0..31)??     */
  BYTE  fcbname[11];  /* name in FCB format         */
  BYTE  unused[8];    /* used by DOS (not us!)      */
  BYTE  mtchattr;     /* attribute of file          */
  WORD  ftime;        /* DOS packed time file       */
  WORD  fdate;        /* DOS packed date of file    */
  DWORD fsize;        /* size of file               */
  BYTE  mtchname[13]; /* ASCIIZ name of file found  */
} DTA;

typedef struct {
  BYTE  drive;        /* drive letter?     */
  BYTE  srchtmpl[11]; /* search template   */
  BYTE  srchattr;     /* search attribute  */
  WORD  direntry;     /* directory entry # */
  WORD  par_clstr;    /* ???               */
  BYTE  unused[4];    /* garbage bytes     */
} SDB;

typedef struct {
  char  fname[11];
  BYTE  attr;
  char  junk[10];
  WORD  ftime;
  WORD  fdate;
  WORD  cluster;
  DWORD size;
} DIR;

typedef struct {
  WORD  handlect,
        open_mode;
  BYTE  attr;
  WORD  dev_info;
  void *devdrv_ptr;
  WORD  start_clstr,    /* not used by me */
        ftime,
        fdate;
  DWORD fsize,
        fpos;
  WORD  rel_lastclu,    /* not used by me */
        abs_lastclu,    /* not used by me */
        dir_sector;     /* not used by me */
  BYTE  dir_entryno;    /* not used by me */
  BYTE  fcb_name[11];
} SFT;

typedef struct tagSYS_FTAB {
  struct tagSYS_FTAB *next;
  WORD num_files;
  BYTE files[];
} SYS_FTAB;

typedef struct {
  BYTE type;
  WORD owner;
  WORD size;
  BYTE stuff[11];
} MCB;

typedef struct {
  WORD mcode;
  WORD mem_top;
  BYTE junk1;
  BYTE longcall[5];
  DWORD int22;
  DWORD int23;
  DWORD int24;
  WORD  parent;
  BYTE  handles[20];
  WORD  env;
  DWORD stack;
  WORD  handlesize;
  DWORD handlepos;
} PSP;

/*
  length includes length of header block
*/
typedef struct {
  BYTE  packetID[2];  /* ................................... */
  WORD  length;       /* total number of bytes in this block */
  WORD  notlength;    /* ~ length (used for checking)        */
  WORD  cmd;          /* command to execute / result         */
  WORD  sender_id;    /* machine ID of block sender          */
  WORD  address_id;   /* machine ID of intended reciever     */
  WORD  process_id;   /* ID of sending process (for closeall)*/
  DWORD crc32;        /* crc-32 of entire block              */
  BYTE  data[];       /* misc. data                          */
} XMITBUF;

/*
  known commands
  note:
    open / create --> (map to) extopen
*/
enum {
  IFS_RMDIR,
  IFS_MKDIR,
  IFS_CHDIR,
  IFS_CLOSEFILE,
  IFS_COMMITFILE,
  IFS_READFILE,
  IFS_WRITEFILE,
  IFS_LOCKFILE,
  IFS_UNLOCKFILE,
  IFS_GETSPACE,
  IFS_SETATTR,
  IFS_GETATTR,
  IFS_RENAMEFILE,
  IFS_DELETEFILE,
  IFS_OPENFILE,
  IFS_CREATEFILE,
  IFS_FINDFIRST,
  IFS_FINDNEXT,
  IFS_CLOSEALL,
  IFS_EXTOPEN,
  IFS_ENDOFLIST
};

typedef struct {
  DWORD lenfail;  /* number of length mismatch failures */
  DWORD crcfail;  /* number of CRC mismatch failures    */
  DWORD valid;    /* number of valid bytes recieved     */
  DWORD timeout;  /* number of timeouts                 */
  DWORD totalrcvd;/* number of bytes received           */
  DWORD totalsent;/* number of bytes sent               */
  WORD  stackused;/* number of bytes of local stack used*/
  DWORD inserver; /* number of calls to server          */
  unsigned char *openhandle; /* pointer to open files handles   */
  unsigned *openpsp;    /* pointer to open files psp       */
} IFS_STAT;

/*
  this is how the registers are accessed on entry
*/
typedef struct {
  WORD bp,
       es,
       ds,
       di,
       si,
       dx,
       cx,
       bx,
       ax,
       ip,
       cs,
       flags,
       wparam;      /* extra parameter word */
} INTREGS;

/*
  most programs do block reads/writes in multiples of 512, 1024, 2048, etc.
  so this minimizes the number of partial block sent.
  the extra 8 bytes are for header info.
  the actual block size is BLOCKSIZE + sizeof(XMITBUF)
*/
#define BLOCKSIZE 1032
#define MAXOPEN 20

#endif
