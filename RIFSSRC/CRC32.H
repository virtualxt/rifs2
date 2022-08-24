#ifndef crc32_h
#define crc32_h

unsigned long far crc32(long crc, void *buf, unsigned length);
void          far memswap(void *a, void *b, unsigned length);

#endif
