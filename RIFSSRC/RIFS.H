#ifndef rifs_h
#define rifs_h

#if 0
#define BYTE  unsigned char
#define WORD unsigned int
#define DWORD unsigned long

#define BOOL  BYTE

enum {FALSE, TRUE};
#endif

void  ArgInit(int argc, char **argv);
void  ArgError(char *err);
int   GetOption(char **optionlist);
int   FindRIFS(void);
int   FindUnusedInt(void);
char *GetArg(void);

#endif
