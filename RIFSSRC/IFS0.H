#ifndef ifs0_h
#define ifs0_h

void far ifs0_init(unsigned intno);
void far ifs0_shutdown(void);
unsigned far ifs0_GetStackUsed(void);
#endif

