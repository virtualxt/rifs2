#ifndef svr0_h
#define svr0_h

void     far svr0_init(unsigned intno);
void     far svr0_shutdown(void);
unsigned far svr0_GetSemaphores(void);
void     far svr0_SetDispatchFlag(void);
void     far svr0_ResetDispatchFlag(void);
unsigned far svr0_GetStackUsed(void);
void     far svr0_TestCallSvr(void);
void     far SwapDOS(void);

void     interrupt svr0_int1b(void);
void     interrupt svr0_int23(void);
void     interrupt svr0_int24(void);
#endif
