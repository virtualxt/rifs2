#ifndef comio_h

#ifndef DWORD
#define DWORD unsigned long
#endif

#define PARITY_EVEN 0x18
#define PARITY_ODD  0x08
#define PARITY_NONE 0x00
#define WORD_5      0x00
#define WORD_6      0x01
#define WORD_7      0x02
#define WORD_8      0x03
#define STOP_1      0x00
#define STOP_2      0x04

int  CommIO_Initialize(int com, int setup, long baud, int irq, int baseport);
void CommIO_shutdown(void);
void CommIO_Transmit(void *buf, int len);
void CommIO_TransmitLoop(void *buf, int len);
int  CommIO_GetByte(void);
int  CommIO_GetStatus(void);
int  CommIO_WaitByte(unsigned timeout);
int  CommIO_WaitByteLoop(unsigned timeout);
int  CommIO_RecievePending(void);
int  CommIO_TransmitPending(void);
int  CommIO_InitRecieveCallback(void (*cb)(void));
void CommIO_FlushBuffer(void);

extern DWORD total_sent;
extern DWORD total_rcvd;
#endif
