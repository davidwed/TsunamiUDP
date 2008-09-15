/*========================================================================
 * tsunami.h  --  Global header file for the Tsunami protocol suite.
 *
 * This file contains global definitions and function prototypes for
 * the Tsunami protocol suite.
 *
 * Written by Mark Meiss (mmeiss@indiana.edu).
 * Copyright 2002 The Trustees of Indiana University.
 * All rights reserved.
 *
 * Pretty much rewritten by Jan Wagner (jwagner@wellidontwantspam)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1) All redistributions of source code must retain the above
 *    copyright notice, the list of authors in the original source
 *    code, this list of conditions and the disclaimer listed in this
 *    license;
 *
 * 2) All redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the disclaimer
 *    listed in this license in the documentation and/or other
 *    materials provided with the distribution;
 *
 * 3) Any documentation included with all redistributions must include
 *    the following acknowledgement:
 *
 *      "This product includes software developed by Indiana
 *      University`s Advanced Network Management Lab. For further
 *      information, contact Steven Wallace at 812-855-0960."
 *
 *    Alternatively, this acknowledgment may appear in the software
 *    itself, and wherever such third-party acknowledgments normally
 *    appear.
 *
 * 4) The name "tsunami" shall not be used to endorse or promote
 *    products derived from this software without prior written
 *    permission from Indiana University.  For written permission,
 *    please contact Steven Wallace at 812-855-0960.

 * 5) Products derived from this software may not be called "tsunami",
 *    nor may "tsunami" appear in their name, without prior written
 *    permission of Indiana University.
 *
 * Indiana University provides no reassurances that the source code
 * provided does not infringe the patent or any other intellectual
 * property rights of any other entity.  Indiana University disclaims
 * any liability to any recipient for claims brought by any other
 * entity based on infringement of intellectual property rights or
 * otherwise.
 *
 * LICENSEE UNDERSTANDS THAT SOFTWARE IS PROVIDED "AS IS" FOR WHICH
 * NO WARRANTIES AS TO CAPABILITIES OR ACCURACY ARE MADE. INDIANA
 * UNIVERSITY GIVES NO WARRANTIES AND MAKES NO REPRESENTATION THAT
 * SOFTWARE IS FREE OF INFRINGEMENT OF THIRD PARTY PATENT, COPYRIGHT,
 * OR OTHER PROPRIETARY RIGHTS. INDIANA UNIVERSITY MAKES NO
 * WARRANTIES THAT SOFTWARE IS FREE FROM "BUGS", "VIRUSES", "TROJAN
 * HORSES", "TRAP DOORS", "WORMS", OR OTHER HARMFUL CODE.  LICENSEE
 * ASSUMES THE ENTIRE RISK AS TO THE PERFORMANCE OF SOFTWARE AND/OR
 * ASSOCIATED MATERIALS, AND TO THE PERFORMANCE AND VALIDITY OF
 * INFORMATION GENERATED USING SOFTWARE.
 *========================================================================*/

#ifndef _TSUNAMI_H
#define _TSUNAMI_H

#include <sys/types.h>  /* for u_char, u_int16_t, etc. */
#include <sys/time.h>   /* for struct timeval          */
#include <stdio.h>      /* for NULL, FILE *, etc.      */

#include "tsunami-cvs-buildnr.h"   /* for the current TSUNAMI_CVS_BUILDNR */


/*------------------------------------------------------------------------
 * Macro definitions.
 *------------------------------------------------------------------------*/

#define warn(message)   error_handler(__FILE__, __LINE__, (message), 0)
#define error(message)  error_handler(__FILE__, __LINE__, (message), 1)

#define min(a,b)  (((a) < (b)) ? (a) : (b))
#define max(a,b)  (((a) > (b)) ? (a) : (b))

#define tv_diff_usec(newer,older) ((newer.tv_sec-older.tv_sec)*1e6 + (newer.tv_usec-older.tv_usec))

#define read_into(fd,var)      read(fd, &var, sizeof(var))          /* safe'ish read from file/stream into variable */
#define fread_into(fd,var)    fread(&var, sizeof(var), 1, fd)
#define write_from(fd,var)    write(fd, &var, sizeof(var))         /* safe'ish write from variable to file/stream  */
#define fwrite_from(fd,var)  fwrite(&var, sizeof(var), 1, fd)

#define net_to_host(var) \
    if      (sizeof(var) == sizeof(u_int16_t)) var = ntohs(var); \
    else if (sizeof(var) == sizeof(u_int32_t)) var = ntohl(var); \
    else if (sizeof(var) == sizeof(u_int64_t)) var = ntohll(var)  /* convert variable from net to host presentation */

#define host_to_net(var) \
    if      (sizeof(var) == sizeof(u_int16_t)) var = htons(var); \
    else if (sizeof(var) == sizeof(u_int32_t)) var = htonl(var); \
    else if (sizeof(var) == sizeof(u_int64_t)) var = htonll(var)  /* convert variable from host to net presentation */


typedef unsigned long long ull_t;

/*------------------------------------------------------------------------
 * Global constants.
 *------------------------------------------------------------------------*/

#define MAX_ERROR_MESSAGE  512        /* maximum length of an error message */
#define MAX_BLOCK_SIZE     65530      /* maximum size of a data block       */

extern const u_int32_t PROTOCOL_REVISION;

extern const u_int16_t REQUEST_RETRANSMIT;
extern const u_int16_t REQUEST_RESTART;
extern const u_int16_t REQUEST_STOP;
extern const u_int16_t REQUEST_ERROR_RATE;

#define  TS_TCP_PORT    46224   /* default TCP port of the remote server        */
#define  TS_UDP_PORT    46224   /* default UDP port of the client / 47221       */

#define  TS_BLOCK_ORIGINAL          'O'   /* blocktype "original block" */
#define  TS_BLOCK_TERMINATE         'X'   /* blocktype "end transmission" */
#define  TS_BLOCK_RETRANSMISSION    'R'   /* blocktype "retransmitted block" */

#define  TS_DIRLIST_HACK_CMD        "!#DIR??" /* "file name" sent by the client to request a list of the shared files */

#define  UPDATE_PERIOD              350000LL  /* length of the update/heartbeat period in usec */

/*------------------------------------------------------------------------
 * Data structures.
 *------------------------------------------------------------------------*/

/* try to avoid compiler struct padding -- if structs are placed over network data... */
#pragma pack(1)

/* retransmission request */
typedef struct {
    u_int16_t           request_type;  /* the retransmission request type           */
    u_int64_t           block;         /* the block number to retransmit {at}       */
    u_int32_t           error_rate;    /* the current error rate (in % x 1000)      */
} retransmission_t;

typedef struct {
    u_int64_t           block;         /* the block number                          */
    u_int16_t           type;          /* the block type                            */
} blockheader_t;

#pragma pack()

/*------------------------------------------------------------------------
 * Global variables.
 *------------------------------------------------------------------------*/

extern char g_error[];  /* buffer for the most recent error string    */


/*------------------------------------------------------------------------
 * Function prototypes.
 *------------------------------------------------------------------------*/

/* common.c */
int        get_random_data         (u_char *buffer, size_t bytes);
u_int64_t  get_usec_since          (struct timeval *old_time);
u_int64_t  htonll                  (u_int64_t value);
char      *make_transcript_filename(char *buffer, time_t epoch, const char *extension);
u_int64_t  ntohll                  (u_int64_t value);
u_char    *prepare_proof           (u_char *buffer, size_t bytes, const u_char *secret, u_char *digest);
int        read_line               (int fd, char *buffer, size_t buffer_length);
int        fread_line              (FILE *f, char *buffer, size_t buffer_length);
void       usleep_that_works       (u_int64_t usec);
u_int64_t  get_udp_in_errors       ();

/* error.c */
int        error_handler           (const char *file, int line, const char *message, int fatal_yn);

#endif // _TSUNAMI_H

/*========================================================================
 * $Log$
 * Revision 1.8.2.5  2007/12/07 15:10:05  jwagnerhki
 * pragma backwards comp fix, read_vsib_block calls with session
 *
 * Revision 1.8.2.4  2007/11/12 15:03:05  jwagnerhki
 * defined UPDATE_PERIOD for both client and server, server 'no heartbeat' messages only after UPDATE_PERIOD usec passed
 *
 * Revision 1.8.2.3  2007/11/10 14:49:24  jwagnerhki
 * first try at 64-bit 'clean' compile
 *
 * Revision 1.8.2.2  2007/11/10 14:15:17  jwagnerhki
 * pragma pack(1)
 *
 * Revision 1.8.2.1  2007/11/09 22:43:52  jwagnerhki
 * protocol v1.2 build 1
 *
 *
 */
