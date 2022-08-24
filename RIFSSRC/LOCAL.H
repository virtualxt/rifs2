#ifndef local_h
#define local_h

enum {FALSE, TRUE};

#define LOCAL static

#define BYTE  unsigned char
#define WORD  unsigned int
#define DWORD unsigned long

#define BOOL  BYTE

#define CLOCK (*(long *) MK_FP(0x40, 0x6c))
#define NORMALIZE(a) a=MK_FP(FP_SEG(a)+(FP_OFF(a) >> 4), FP_OFF(a) & 0x0f)

#endif
