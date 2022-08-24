#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>

#include "rifs.h"

static char **g_argv; /* argv[] as passed into main()  */
static int    g_argc; /* argc   as passed into main()  */
static int    g_argp; /* pointer to next argv[]        */
static char  *g_arga; /* argument to last found option */

void ArgInit(int argc, char **argv)
{
  g_argv=argv;
  g_argc=argc;
  g_argp=1;
  g_arga=NULL;
}

void ArgError(char *err)
{
  fprintf(stderr, "%s\n", err);
  exit(1);
}

int GetOption(char **optionlist)
{
  int     ii;
  char  *ptr,
       **opt;
  int    len;

  if (g_argp == g_argc)
    return -1;

  for (ii=0, opt=optionlist; *opt; opt++, ii++) {
    ptr = strchr(*opt, '=');
    len = (ptr) ? (ptr-*opt) : strlen(*opt);
    if (strnicmp(g_argv[g_argp], *opt, len) == 0)
      break;
  }
  if (!*opt)
    ArgError("option not recognized");
  if (ptr) {
    if (g_argv[g_argp][len]) {
      if (g_argv[g_argp][len] != '=')
        ArgError("syntax error");
      else if (g_argv[g_argp][len+1]) {
        g_arga=g_argv[g_argp]+len+1;
        g_argp++;
      } else if (++g_argp == g_argc)
        ArgError("syntax error");
      else
        g_arga=g_argv[g_argp++];
    } else {
      g_argp++;
      if ((g_argp == g_argc) || (g_argv[g_argp][0] != '='))
        ArgError("syntax error");
      else if (g_argv[g_argp][1]) {
        g_arga=g_argv[g_argp]+1;
        g_argp++;
      } else if (++g_argp == g_argc)
        ArgError("syntax error");
      else {
        g_arga=g_argv[g_argp];
        g_argp++;
      }
    }
  } else {
    if (strlen(*opt) > strlen(g_argv[g_argp]))
      ArgError("option not recognized(2)");
    else
      g_argp++;
    g_arga=NULL;
  }
  return ii;
}

/*
  locate the RIFS interrupt in the range 0x60..0x66 OR 0x18
  known by the signature "RIFS" in the four bytes preceding the
  entry address.
  return 0 if none found
*/
static int dataddrs[]={0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x18};

int FindRIFS(void)
{
  char *sig;
  int ii;

  for (ii=0; ii < sizeof(dataddrs); ii++) {
    sig=*(char **) MK_FP(0x0, dataddrs[ii]*4);
    if (sig && (FP_OFF(sig) > 4) && (memcmp(sig-4, "RIFS", 4) == 0))
      return dataddrs[ii];
  }
  return 0;
}

/*
  return the first empty interrupt (points to 0:0)
  0x60..0x66 OR 0x18. -1 if none found
*/
int FindUnusedInt(void)
{
  int ii;

  for (ii=0; ii < sizeof(dataddrs); ii++)
    if (*(char *) MK_FP(0x0, dataddrs[ii]*4) == 0)
      return dataddrs[ii];
  return 0;
}

char *GetArg(void)
{
  return g_arga;
}
