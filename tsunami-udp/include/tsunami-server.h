/*========================================================================
 * tsunami-server.h  --  Global header for Tsunami server.
 *
 * This contains global definitions for the Tsunami file transfer client.
 *
 * Written by Mark Meiss (mmeiss@indiana.edu).
 * Copyright  2002 The Trustees of Indiana University.
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
 * NOWARRANTIES AS TO CAPABILITIES OR ACCURACY ARE MADE. INDIANA
 * UNIVERSITY GIVES NO WARRANTIES AND MAKES NO REPRESENTATION THAT
 * SOFTWARE IS FREE OF INFRINGEMENT OF THIRD PARTY PATENT, COPYRIGHT,
 * OR OTHER PROPRIETARY RIGHTS. INDIANA UNIVERSITY MAKES NO
 * WARRANTIES THAT SOFTWARE IS FREE FROM "BUGS", "VIRUSES", "TROJAN
 * HORSES", "TRAP DOORS", "WORMS", OR OTHER HARMFUL CODE.  LICENSEE
 * ASSUMES THE ENTIRE RISK AS TO THE PERFORMANCE OF SOFTWARE AND/OR
 * ASSOCIATED MATERIALS, AND TO THE PERFORMANCE AND VALIDITY OF
 * INFORMATION GENERATED USING SOFTWARE.
 *========================================================================*/

#ifndef __TSUNAMI_SERVER_H
#define __TSUNAMI_SERVER_H

#include <netinet/in.h>  /* for struct sockaddr_in, etc.                 */
#include <stdio.h>       /* for NULL, FILE *, etc.                       */
#include <sys/types.h>   /* for various system data types                */
#include <mcheck.h>

#include <tsunami.h>     /* for Tsunami function prototypes and the like */


/*------------------------------------------------------------------------
 * Global constants.
 *------------------------------------------------------------------------*/

extern const u_int32_t  DEFAULT_BLOCK_SIZE;         /* default size of a single file block     */
extern const u_char    *DEFAULT_SECRET;             /* default shared secret                   */
extern const u_int16_t  DEFAULT_TCP_PORT;           /* default TCP port to listen on           */
extern const u_int32_t  DEFAULT_UDP_BUFFER;         /* default size of the UDP transmit buffer */
extern const u_char     DEFAULT_VERBOSE_YN;         /* the default verbosity setting           */
extern const u_char     DEFAULT_TRANSCRIPT_YN;      /* the default transcript setting          */
extern const u_char     DEFAULT_IPV6_YN;            /* the default IPv6 setting                */
extern const u_int16_t  DEFAULT_HEARTBEAT_TIMEOUT;  /* the default timeout after no client heartbeat */

#define MAX_FILENAME_LENGTH  1024               /* maximum length of a requested filename  */
#define RINGBUF_BLOCKS  1                       /* Size of ring buffer (disabled now) */
#define FRAMES_IN_SLOT  40                      /* 0.02s timeslots for computers */

/*------------------------------------------------------------------------
 * Data structures.
 *------------------------------------------------------------------------*/

/* VLBI data channel and channel subscription descriptors (for future RF-over-IP) */
typedef struct {
    u_char              bit_depth;      /* bits per sample                     */
    u_char              modulation;     /* LSB, SSB, AM, ..., for demodulation */
    u_int64_t           f_carrier;      /* carrier frequency in Hz             */
    u_int32_t           delta_f;        /* signal or filter bandwidth          */
    u_int32_t           f_s;            /* sampling rate in Hz                 */
    u_char              agc;            /* 0=off, 1=fixed, 2='fast', 3=...     */
    u_int16_t           vbr_flags;      /* 0=none, 1=bandwidth halving, 2=bit reduction, 4=... */
    struct sockaddr    *udp_address;    /* the destination for the data stream */
    socklen_t           udp_length;     /* the length of the UDP sockaddr      */
} ttp_rfchannel_t;

typedef struct {
   u_int16_t            num_channels;   /* how many channels requested                */
   ttp_rfchannel_t      channels[1024]; /* the actual channels with target host+port  */
} ttp_rfoverip_subscription_t;

/* Tsunami transfer protocol parameters */
typedef struct {
    u_int64_t           epoch;          /* the Unix epoch used to identify this run   */
    u_char              verbose_yn;     /* verbose mode (0=no, 1=yes)                 */
    u_char              transcript_yn;  /* transcript mode (0=no, 1=yes)              */
    u_char              ipv6_yn;        /* IPv6 mode (0=no, 1=yes)                    */
    u_int16_t           tcp_port;       /* TCP port number for listening on           */
    u_int32_t           udp_buffer;     /* size of the UDP send buffer in bytes       */
    u_int16_t           hb_timeout;     /* the client heartbeat timeout               */
    const u_char       *secret;         /* the shared secret for users to prove       */
    u_int32_t           block_size;     /* the size of each block (in bytes)          */
    u_int64_t           file_size;      /* the total file size (in bytes)             */
    u_int64_t           block_count;    /* the total number of blocks in the file     */
    u_int64_t           target_rate;    /* the transfer rate that we're targetting    */
    u_int32_t           error_rate;     /* the threshhold error rate (in % x 1000)    */
    u_int32_t           ipd_time;       /* the inter-packet delay in usec             */
    u_int16_t           slower_num;     /* the numerator of the increase-IPD factor   */
    u_int16_t           slower_den;     /* the denominator of the increase-IPD factor */
    u_int16_t           faster_num;     /* the numerator of the decrease-IPD factor   */
    u_int16_t           faster_den;     /* the denominator of the decrease-IPD factor */
    char                *ringbuf;       /* Pointer to ring buffer start               */
    u_int16_t           fileout;        /* Do we store the data to file?              */
    char                **file_names;   /* Store the local file_names on server       */
    size_t              *file_sizes;    /* Store the local file sizes on server       */
    u_int16_t           file_name_size; /* Store the total size of the array          */
    u_int16_t           total_files;    /* Store the total number of served files     */
    long                wait_u_sec;
} ttp_parameter_t;

/* state of a transfer */
typedef struct {
    ttp_parameter_t    *parameter;    /* the TTP protocol parameters                */
    char               *filename;     /* the path to the file                       */
    FILE               *file;         /* the open file that we're transmitting      */
    FILE               *vsib;         /* the vsib file number                       */
    FILE               *transcript;   /* the open transcript file for statistics    */
    int                 udp_fd;       /* the file descriptor of our UDP socket      */
    struct sockaddr    *udp_address;  /* the destination for our file data          */
    socklen_t           udp_length;   /* the length of the UDP socket address       */
    u_int32_t           ipd_current;  /* the inter-packet delay currently in usec   */
    u_int64_t           block;        /* the current block that we're up to         */
} ttp_transfer_t;

/* state of a Tsunami session as a whole */
typedef struct {
    ttp_parameter_t    *parameter;    /* the TTP protocol parameters                */
    ttp_transfer_t      transfer;     /* the current transfer in progress, if any   */
    int                 client_fd;    /* the connection to the remote client        */
    int                 session_id;   /* the ID of the server session, autonumber   */
} ttp_session_t;


/*------------------------------------------------------------------------
 * Function prototypes.
 *------------------------------------------------------------------------*/

/* config.c */
void reset_server         (ttp_parameter_t *parameter);

/* io.c */
int  build_datagram       (ttp_session_t *session, u_int64_t block_index, u_int16_t block_type, u_char *datagram);

/* vsibctl.c */
#ifdef VSIB_REALTIME
void start_vsib (ttp_session_t *session);
void stop_vsib (ttp_session_t *session);
void read_vsib_block(ttp_session_t* session, unsigned char *memblk, size_t blksize);
#endif

/* log.c */
/* void log                  (FILE *log_file, const char *format, ...); */

/* network.c */
int  create_tcp_socket    (ttp_parameter_t *parameter);
int  create_udp_socket    (ttp_parameter_t *parameter);

/* protocol.c */
int  ttp_accept_retransmit(ttp_session_t *session, retransmission_t *retransmission, u_char *datagram);
int  ttp_authenticate     (ttp_session_t *session, const u_char *secret);
int  ttp_negotiate        (ttp_session_t *session);
int  ttp_open_port        (ttp_session_t *session);
int  ttp_open_transfer    (ttp_session_t *session);

/* transcript.c */
void xscript_close        (ttp_session_t *session, u_int64_t delta);
void xscript_data_log     (ttp_session_t *session, const char *logline);
void xscript_data_start   (ttp_session_t *session, const struct timeval *epoch);
void xscript_data_stop    (ttp_session_t *session, const struct timeval *epoch);
void xscript_open         (ttp_session_t *session);

#endif /* __TSUNAMI_SERVER_H */


/*========================================================================
 * $Log$
 * Revision 1.10.2.2  2007/11/12 14:43:11  jwagnerhki
 * sizeof((int16*)port) fixed, send epoch as 64-bit
 *
 * Revision 1.10.2.1  2007/11/09 22:43:52  jwagnerhki
 * protocol v1.2 build 1
 *
 *
 */
