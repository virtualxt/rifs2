/*
  serial i/o stuff. does not use the 16550a FIFO.
  i do not track line errors such as parity, overrun, or framing as
  it would require 2 bytes / character instead of 1 and i want to
  save some memory. since i do various block checks internally, it
  is also redundant.
*/
#include <stdlib.h>
#include <stdio.h>
#include <dos.h>
#include <conio.h>
#include <string.h>

#include "comio.h"
#include "myalloc.h"

extern void com0_init(void);
extern void com0_shutdown(void);
extern void interrupt com0();

#define BYTE unsigned char
#define WORD unsigned int
#define DWORD unsigned long

#define CLOCK (*(long *) MK_FP(0x40, 0x6c))

struct {
  BYTE key;
  BYTE attr;
} *VIDMEM;

/*
    port 0x3f8 / irq 4 / int c = #1
    port 0x2f8 / irq 3 / int b = #2
*/

/*
  transmit / recieve buffer
*/
int TRB, /* transmit/recieve character buffer */
    IER, /* interrupt enable                  */
    IIR, /* interrupt identification          */
    LCR, /* line control                      */
    MCR, /* modem control                     */
    LSR, /* line status                       */
    MSR; /* modem status                      */

int    xmitlen;      /* number of bytes left to transmit  */
BYTE  *xmitptr;      /* next byte in transmit buffer      */
BYTE  *rcvbuf;       /* recieve buffer (queue)            */

int    rcv_head,
       rcv_tail,
       rcv_size;     /* actual buffer size MUST BE one less than a power
                        of 2 */

static int    irq;          /* irq address                       */
static int    irq_int;      /* interrupt address                 */
static void interrupt (*oldint14)();

DWORD  total_sent;
DWORD  total_rcvd;

/*
  i read as a footnote somewhere that the IIR bits are not always
  accurate, thus the reason i check LSR before doing any work...

  i loop in here to catch high speed transmission in bursts
*/
void CommIO(void)
{
  do {
    int stat=inportb(LSR);         /* get line status */
    inportb(MSR);                  /* ignore modem status */
    /*
      if byte pending in buffer, get it
    */
    if (stat & 0x01) {
      int byte=inportb(TRB);
      total_rcvd++;

#if 0
      /*
        this code is for debugging only. it displays the incoming characters
        on the top line of the screen
      */
      {
        int pos=total_rcvd % 80;
        VIDMEM[pos].key=byte;
        VIDMEM[pos].attr=0x07;
      }
#endif

      if (((rcv_head+1) & rcv_size) != rcv_tail)
        rcvbuf[rcv_head++]=byte;
      rcv_head &= rcv_size;
    }

    /*
      if byte ready to be sent, send it
    */
    if (xmitlen && (stat & 0x20)) {
      total_sent++;
#if 0
      {
        int pos=total_sent % 80;
        VIDMEM[80+pos].key=*xmitptr;
        VIDMEM[80+pos].attr=0x07;
      }
#endif
      outportb(TRB, *(xmitptr++));
      xmitlen--;
    }
  } while ((!(inportb(IIR) & 0x01)));
}

void CommIO_shutdown(void)
{
  if (irq_int) {
    outportb(0x21, inportb(0x21) | (1 << irq));
    setvect(irq_int, *oldint14);
    irq_int=0;
    outportb(IER, 0);
    com0_shutdown();
    my_free(rcvbuf);
  }
}

int MODE;
/*
  com       = 1, 2, 3, 4
  mode      = or'd options
  baud      = baud rate
  pirq      = irq # override (-1 for default), 0 = use polled
  pbaseport = base port override (-1 for default)
return
  0 if initialized, else fail code
*/
int CommIO_Initialize(int  com,
                      int  mode,
                      long baud,
                      int  pirq,
                      int  pbaseport)
{
  int base;

  MODE=mode;
  xmitlen=0;
  xmitptr=0;
  rcv_size=2047;
  rcvbuf=my_malloc(rcv_size+1);
  if (!rcvbuf)
    return 1;

  VIDMEM = (*(WORD *) MK_FP(0x40, 0x63) == 0x3b4) ?
    MK_FP(0xb000, 0x0000) :
    MK_FP(0xb800, 0x0000);
  com--;
  base    = (pbaseport < 0) ? *(WORD *) MK_FP(0x40, 2*com) : pbaseport;
  irq     = (pirq < 0) ? 0x04-(com & 0x01): pirq;
  irq_int = irq ? 0x08+irq : 0;

  TRB =(base);
  IER =(base+1);
  IIR =(base+2);
  LCR =(base+3);
  MCR =(base+4);
  LSR =(base+5);
  MSR =(base+6);

  baud = 115200/baud;
  if (!baud)   /* must be at least 1 */
    baud++;
  outportb(LCR, mode | 0x80);           /* set baud rate & mode */
  outportb(TRB, baud & 0xff);
  outportb(TRB+1, baud >> 8);
  outportb(LCR, mode);
  outportb(MCR, 0);

  com0_init();

  if (irq_int) {
    oldint14=getvect(irq_int);
    setvect(irq_int, com0);
    outportb(0x21, inportb(0x21) & ~(1 << irq)); /* enable the interrupt */
    outportb(IER, 0x03);           /* enable line status change interrupt */
    do {
      inportb(TRB);   /* clear any pending bytes        */
      inportb(LSR);   /* clear the line status register */
      inportb(MSR);
    } while (!(inportb(IIR) & 0x01));
    outportb(MCR, 0x0c);
  } else {
    outportb(IER, 0x00);
    outportb(MCR, 0x00);
  }
  return 0;
}

void CommIO_Transmit(void *buf, int len)
{
  xmitlen=0;                           /* clear pending xmit            */
  while ((inportb(LSR) & 0x20) == 0);  /* wait for xmit buffer to empty */
  if (len) {
    xmitptr=((char *) buf)+1;
    outportb(TRB, *(char *) buf);        /* transmit first character      */
    xmitlen=len-1;                       /* set length to buffer - 1      */
  }
}

/*
  return the next byte in the queue, -1 on error
*/
int  CommIO_GetByte(void)
{
  if (rcv_head == rcv_tail)
    return -1;
  else {
    int byte=rcvbuf[rcv_tail++];
    rcv_tail &= rcv_size;
    return byte;
  }
}

/*
  return TRUE if there are bytes in the queue, else FALSE
*/
int  CommIO_RecievePending(void)
{
  return (rcv_tail != rcv_head);
}

/*
  return TRUE if we are mid-transmit, else FALSE
*/
int CommIO_TransmitPending(void)
{
  return (xmitlen);
}

/*
  return >= 0 == byte
         <  0 == error
  timeout in 1/18 sec
*/
int  CommIO_WaitByte(unsigned timeout)
{
  long clk=CLOCK;

  while (!CommIO_RecievePending() && (labs(CLOCK-clk) < timeout));
  return CommIO_GetByte();
}

/*
  flush the recieve queue
*/
void  CommIO_FlushBuffer(void)
{
  rcv_head=rcv_tail=0;
}

void CommIO_TransmitLoop(void *buf, int len)
{
  char *out=buf;

  xmitlen=0;                           /* clear pending xmit            */
  outportb(LCR, MODE);
  while ((inportb(LSR) & 0x20) == 0);  /* wait for xmit buffer to empty */
  while (len--) {
#if 0
    VIDMEM[80 + total_sent % 80].key=*out;
#endif
    outportb(TRB, *(out++));
    total_sent++;
    while ((inportb(LSR) & 0x20) == 0);  /* wait for xmit buffer to empty */
  }
}

int  CommIO_WaitByteLoop(unsigned timeout)
{
  long clk=CLOCK;

  while (!(inportb(LSR) & 0x01)) {
    if (labs(CLOCK-clk) >= timeout) {
      outportb(LCR, MODE);
      return -1;
    }
  }
  total_rcvd++;
  {
    int key=inportb(TRB);
#if 0
    VIDMEM[total_rcvd % 80].key=key;
#endif
    return key;
  }
}
