/*========================================================================
 * client.h  --  Global header for Tsunami client.
 *
 * This contains global definitions for the Tsunami file transfer client.
 *
 * Written by Mark Meiss (mmeiss@indiana.edu).
 * Copyright � 2002 The Trustees of Indiana University.
 * All rights reserved.
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
 *
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
 * NO�WARRANTIES AS TO CAPABILITIES OR ACCURACY ARE MADE. INDIANA
 * UNIVERSITY GIVES NO WARRANTIES AND MAKES NO REPRESENTATION THAT
 * SOFTWARE IS FREE OF INFRINGEMENT OF THIRD PARTY PATENT, COPYRIGHT,
 * OR OTHER PROPRIETARY RIGHTS.� INDIANA UNIVERSITY MAKES NO
 * WARRANTIES THAT SOFTWARE IS FREE FROM "BUGS", "VIRUSES", "TROJAN
 * HORSES", "TRAP DOORS", "WORMS", OR OTHER HARMFUL CODE.  LICENSEE
 * ASSUMES THE ENTIRE RISK AS TO THE PERFORMANCE OF SOFTWARE AND/OR
 * ASSOCIATED MATERIALS, AND TO THE PERFORMANCE AND VALIDITY OF
 * INFORMATION GENERATED USING SOFTWARE.
 *========================================================================*/

#ifndef __CLIENT_H
#define __CLIENT_H

#include <netinet/in.h>  /* for struct sockaddr_in, etc.                 */
#include <stdio.h>       /* for NULL, FILE *, etc.                       */
#include <sys/types.h>   /* for various system data types                */

#include "tsunami.h"     /* for Tsunami function prototypes and the like */


/*------------------------------------------------------------------------
 * Global constants.
 *------------------------------------------------------------------------*/

extern const u_int32_t  DEFAULT_BLOCK_SIZE;     /* default size of a single file block          */
extern const int        DEFAULT_TABLE_SIZE;     /* initial size of the retransmission table     */
extern const char      *DEFAULT_SERVER_NAME;    /* default name of the remote server            */
extern const u_int16_t  DEFAULT_SERVER_PORT;    /* default TCP port of the remote server        */
extern const u_int16_t  DEFAULT_CLIENT_PORT;    /* default UDP port of the client               */
extern const u_int32_t  DEFAULT_UDP_BUFFER;     /* default size of the UDP receive buffer       */
extern const u_char     DEFAULT_VERBOSE_YN;     /* the default verbosity setting                */
extern const u_char     DEFAULT_TRANSCRIPT_YN;  /* the default transcript setting               */
extern const u_char     DEFAULT_IPV6_YN;        /* the default IPv6 setting                     */
extern const u_char     DEFAULT_OUTPUT_MODE;    /* the default output progress mode             */
extern const u_int32_t  DEFAULT_TARGET_RATE;    /* the default target rate (in bps)             */
extern const u_int32_t  DEFAULT_ERROR_RATE;     /* the default threshhold error rate (% x 1000) */
extern const u_int16_t  DEFAULT_SLOWER_NUM;     /* default numerator in the slowdown factor     */
extern const u_int16_t  DEFAULT_SLOWER_DEN;     /* default denominator in the slowdown factor   */
extern const u_int16_t  DEFAULT_FASTER_NUM;     /* default numerator in the speedup factor      */
extern const u_int16_t  DEFAULT_FASTER_DEN;     /* default denominator in the speedup factor    */
extern const u_int16_t  DEFAULT_HISTORY;        /* default percentage of history in rates       */

#define SCREEN_MODE                0            /* screen-based output mode                     */
#define LINE_MODE                  1            /* line-based (vmstat-like) output mode         */

#define MAX_COMMAND_WORDS          10           /* maximum number of words in any command       */
#define MAX_RETRANSMISSION_BUFFER  2048         /* maximum number of requests to send at once   */
#define MAX_BLOCKS_QUEUED          8192         /* maximum number of blocks in ring buffer      */
#define UPDATE_PERIOD              350000LL     /* length of the update period in microseconds  */

extern const int        MAX_COMMAND_LENGTH;     /* maximum length of a single command           */

#define RINGBUF_BLOCKS  1                       /* Size of ring buffer (disabled now)           */
#define START_VSIB_PACKET  14500                /* When to start output                         */
                                                /* 2000 packets per second, now 8 second delay  */

/*------------------------------------------------------------------------
 * Data structures.
 *------------------------------------------------------------------------*/

/* data structure for a parsed command */
typedef struct {
    u_char              count;                    /* number of words in the command              */
    const char         *text[MAX_COMMAND_WORDS];  /* array of pointers to the words              */
} command_t;

/* statistical data */
typedef struct {
    struct timeval      start_time;               /* when we started timing the transfer         */
    struct timeval      this_time;                /* when we began this data collection period   */
    u_int32_t           this_blocks;              /* the number of blocks in this interval       */
    u_int32_t           this_retransmits;         /* the number of retransmits in this interval  */
    u_int32_t           total_blocks;             /* the total number of blocks transmitted      */
    u_int32_t           total_retransmits;        /* the total number of retransmissions         */
    u_int64_t           transmit_rate;            /* the smoothed transmission rate (bps)        */
    u_int64_t           retransmit_rate;          /* the smoothed retransmisson rate (% x 1000)  */
} statistics_t;

/* state of the retransmission table for a transfer */
typedef struct {
    u_int32_t          *table;                    /* the table of retransmission blocks          */
    u_int32_t           table_size;               /* the size of the retransmission table        */
    u_int32_t           index_max;                /* the maximum table index in active use       */
} retransmit_t;

/* ring buffer for queuing blocks to be written to disk */
typedef struct {
    u_char             *datagrams;                /* the collection of queued datagrams          */
    int                 datagram_size;            /* the size of a single datagram               */
    int                 base_data;                /* the index of the first slot with data       */
    int                 count_data;               /* the number of slots in use for data         */
    int                 count_reserved;           /* the number of slots reserved without data   */
    pthread_mutex_t     mutex;                    /* a mutex to guard the ring buffer            */
    pthread_cond_t      data_ready_cond;          /* condition variable to indicate data ready   */
    int                 data_ready;               /* nonzero when data is ready, else 0          */
    pthread_cond_t      space_ready_cond;         /* condition variable to indicate space ready  */
    int                 space_ready;              /* nonzero when space is available, else 0     */
} ring_buffer_t;

/* Tsunami transfer protocol parameters */
typedef struct {
    char               *server_name;              /* the name of the host running tsunamid       */
    u_int16_t           server_port;              /* the TCP port on which the server listens    */
    u_int16_t           client_port;              /* the UDP port on which the client receives   */
    u_int32_t           udp_buffer;               /* the size of the UDP receive buffer in bytes */
    u_char              verbose_yn;               /* 1 for verbose mode, 0 for quiet             */
    u_char              transcript_yn;            /* 1 for transcripts on, 0 for no transcript   */
    u_char              ipv6_yn;                  /* 1 for IPv6, 0 for IPv4                      */
    u_char              output_mode;              /* either SCREEN_MODE or LINE_MODE             */
    u_int32_t           block_size;               /* the size of each block (in bytes)           */
    u_int32_t           target_rate;              /* the transfer rate that we're targetting     */
    u_int32_t           error_rate;               /* the threshhold error rate (in % x 1000)     */
    u_int16_t           slower_num;               /* the numerator of the increase-IPD factor    */
    u_int16_t           slower_den;               /* the denominator of the increase-IPD factor  */
    u_int16_t           faster_num;               /* the numerator of the decrease-IPD factor    */
    u_int16_t           faster_den;               /* the denominator of the decrease-IPD factor  */
    u_int16_t           history;                  /* percentage of history to keep in rates      */
    u_char              no_retransmit;            /* for testing, actual retransmission can be disabled */
    char                *ringbuf;                 /* Pointer to ring buffer start                */
} ttp_parameter_t;    

/* state of a TTP transfer */
typedef struct {
    time_t              epoch;                    /* the Unix epoch used to identify this run    */
    const char         *remote_filename;          /* the path to the file (on the server)        */
    const char         *local_filename;           /* the path to the file (locally)              */
    FILE               *file;                     /* the open file that we're receiving          */
    FILE               *vsib;                     /* the vsib file number                        */
    FILE               *transcript;               /* the transcript file that we're writing to   */
    int                 udp_fd;                   /* the file descriptor of our UDP socket       */
    u_int64_t           file_size;                /* the total file size (in bytes)              */
    u_int32_t           block_count;              /* the total number of blocks in the file      */
    u_int32_t           next_block;               /* the index of the next block we expect       */
    retransmit_t        retransmit;               /* the retransmission data for the transfer    */
    statistics_t        stats;                    /* the statistical data for the transfer       */
    ring_buffer_t      *ring_buffer;              /* the blocks waiting for a disk write         */
    u_char             *received;                 /* bitfield for the received blocks of data    */
    u_int32_t           blocks_left;              /* the number of blocks left to receive        */
} ttp_transfer_t;

/* state of a Tsunami session as a whole */
typedef struct {
    ttp_parameter_t    *parameter;                /* the TTP protocol parameters                 */
    ttp_transfer_t      transfer;                 /* the current transfer in progress, if any    */
    FILE               *server;                   /* the connection to the remote server         */
    struct sockaddr    *server_address;           /* the socket address of the remote server     */
    socklen_t           server_address_length;    /* the size of the socket address              */
} ttp_session_t;


/*------------------------------------------------------------------------
 * Function prototypes.
 *------------------------------------------------------------------------*/

/* command.c */
int            command_close         (command_t *command, ttp_session_t *session);
ttp_session_t *command_connect       (command_t *command, ttp_parameter_t *parameter);
int            command_get           (command_t *command, ttp_session_t *session);
int            command_help          (command_t *command, ttp_session_t *session);
int            command_quit          (command_t *command, ttp_session_t *session);
int            command_set           (command_t *command, ttp_parameter_t *parameter);

/* config.c */
void           reset_client          (ttp_parameter_t *parameter);

/* io.c */
int            accept_block          (ttp_session_t *session, u_int32_t block_index, u_char *block);

/* network.c */
int            create_tcp_socket     (ttp_session_t *session, const char *server_name, u_int16_t server_port);
int            create_udp_socket     (ttp_parameter_t *parameter);

/* protocol.c */
int            ttp_authenticate      (ttp_session_t *session, u_char *secret);
int            ttp_negotiate         (ttp_session_t *session);
int            ttp_open_port         (ttp_session_t *session);
int            ttp_open_transfer     (ttp_session_t *session, const char *remote_filename, const char *local_filename);
int            ttp_repeat_retransmit (ttp_session_t *session);
int            ttp_request_retransmit(ttp_session_t *session, u_int32_t block);
int            ttp_request_stop      (ttp_session_t *session);
int            ttp_update_stats      (ttp_session_t *session);

/* ring.c */
int            ring_cancel           (ring_buffer_t *ring);
int            ring_confirm          (ring_buffer_t *ring);
ring_buffer_t *ring_create           (ttp_session_t *session);
int            ring_destroy          (ring_buffer_t *ring);
int            ring_dump             (ring_buffer_t *ring, FILE *out);
u_char        *ring_peek             (ring_buffer_t *ring);
int            ring_pop              (ring_buffer_t *ring);
u_char        *ring_reserve          (ring_buffer_t *ring);

/* transcript.c */
void           xscript_close         (ttp_session_t *session, u_int64_t delta);
void           xscript_data_log      (ttp_session_t *session, const char *logline);
void           xscript_data_start    (ttp_session_t *session, const struct timeval *epoch);
void           xscript_data_stop     (ttp_session_t *session, const struct timeval *epoch);
void           xscript_open          (ttp_session_t *session);

#endif /* __CLIENT_H */


/*========================================================================
 * $Log$
 * Revision 1.2  2006/07/20 12:23:45  jwagnerhki
 * header file merge
 *
 * Revision 1.1.1.1  2006/07/20 09:21:18  jwagnerhki
 * reimport
 *
 * Revision 1.1  2006/07/10 12:26:51  jwagnerhki
 * deleted unnecessary files
 *
 */
