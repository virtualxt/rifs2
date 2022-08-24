#ifndef rclient_h
#define rclient_h

enum {
  RCLIENT_QUERY,    /* return AX = 0x1234 if client */
  RCLIENT_UNLOAD,
  RCLIENT_REMAP,    /* BH = local, BL = remote */
  RCLIENT_UNMAP,    /* BL = drive to unmap */
  RCLIENT_UNMAPALL,
  RCLIENT_GETXLAT,   /* ES:BX --> xlate table (BYTE *) */
  RCLIENT_GETSTAT,   /* ES:BX --> IFS_STAT */

  RSERVER_QUERY,     /* return AX = 0x4321 if server */
  RSERVER_UNLOAD,
  RSERVER_GETSTAT,   /* ES:BX --> IFS_STAT */
  RSERVER_RESET      /* close any open files */
};
#endif
