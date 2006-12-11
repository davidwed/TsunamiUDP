/*========================================================================
 * protocol.c  --  TTP routines for Tsunami client.
 *
 * This contains the Tsunami Transfer Protocol API for the Tsunami
 * file transfer client.
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

#include <stdlib.h>       /* for *alloc() and free()               */
#include <string.h>       /* for standard string routines          */
#include <sys/socket.h>   /* for the BSD socket library            */
#include <sys/time.h>     /* for gettimeofday()                    */
#include <time.h>         /* for time()                            */
#include <unistd.h>       /* for standard Unix system calls        */

#include "client.h"
//#define DEBUG_RETX xxx // enable to show retransmit debug infos

/*------------------------------------------------------------------------
 * int ttp_authenticate(ttp_session_t *session, u_char *secret);
 *
 * Given an active Tsunami session, returns 0 if we are able to
 * negotiate authentication successfully and a non-zero value
 * otherwise.
 *
 * The negotiation process works like this:
 *
 *     (1) The server sends 512 bits of random data to the client
 *         [this process].
 *
 *     (2) The client XORs 512 bits of the shared secret onto this
 *         random data and responds with the MD5 hash of the result.
 *
 *     (3) The server does the same thing and compares the result.
 *         If the authentication succeeds, the server transmits a
 *         result byte of 0.  Otherwise, it transmits a non-zero
 *         result byte.
 *------------------------------------------------------------------------*/
int ttp_authenticate(ttp_session_t *session, u_char *secret)
{
    u_char  random[64];  /* the buffer of random data               */
    u_char  digest[16];  /* the MD5 message digest (for the server) */
    u_char  result;      /* the result byte from the server         */
    int     status;      /* return status from function calls       */

    /* read in the shared secret and the challenge */
    status = fread(random, 1, 64, session->server);
    if (status < 64)
	return warn("Could not read authentication challenge from server");

    /* prepare the proof of the shared secret and destroy the password */
    prepare_proof(random, 64, secret, digest);
    while (*secret)
	*(secret++) = '\0';

    /* send the response to the server */
    status = fwrite(digest, 1, 16, session->server);
    if ((status < 16) || fflush(session->server))
	return warn("Could not send authentication response");

    /* read the results back from the server */
    status = fread(&result, 1, 1, session->server);
    if (status < 1)
	return warn("Could not read authentication status");

    /* check the result byte */
    return (result == 0) ? 0 : -1;
}


/*------------------------------------------------------------------------
 * int ttp_negotiate(ttp_session_t *session);
 *
 * Performs all of the negotiation with the remote server that is done
 * prior to authentication.  At the moment, this consists of verifying
 * identical protocol revisions between the client and server.  Returns
 * 0 on success and non-zero on failure.
 *
 * Values are transmitted in network byte order.
 *------------------------------------------------------------------------*/
int ttp_negotiate(ttp_session_t *session)
{
    u_int32_t server_revision;
    u_int32_t client_revision = htonl(PROTOCOL_REVISION);
    int       status;

    /* send our protocol revision number to the server */
    status = fwrite(&client_revision, 4, 1, session->server);
    if ((status < 1) || fflush(session->server))
	return warn("Could not send protocol revision number");

    /* read the protocol revision number from the server */
    status = fread(&server_revision, 4, 1, session->server);
    if (status < 1)
	return warn("Could not read protocol revision number");

    /* compare the numbers */
    return (client_revision == server_revision) ? 0 : -1;
}


/*------------------------------------------------------------------------
 * int ttp_open_transfer(ttp_session_t *session,
 *                       const char *remote_filename,
 *                       const char *local_filename);
 *
 * Tries to create a new TTP file request object for the given session
 * by submitting a file request to the server (which is waiting for
 * the name of a file to transfer).  If the request is accepted, we
 * retrieve the file parameters, open the file for writing, and return
 * 0 for success.  If anything goes wrong, we return a non-zero value.
 *------------------------------------------------------------------------*/
int ttp_open_transfer(ttp_session_t *session, const char *remote_filename, const char *local_filename)
{
    u_char           result;    /* the result byte from the server     */
    u_int32_t        temp;      /* used for transmitting 32-bit values */
    int              status;
    ttp_transfer_t  *xfer  = &session->transfer;
    ttp_parameter_t *param =  session->parameter;

    /* submit the transfer request */
    status = fprintf(session->server, "%s\n", remote_filename);
    if ((status <= 0) || fflush(session->server))
	return warn("Could not request file");

    /* see if the request was successful */
    status = fread(&result, 1, 1, session->server);
    if (status < 1)
	return warn("Could not read response to file request");

    /* make sure the result was a good one */
    if (result != 0)
	return warn("Server: File does not exist or cannot be transmitted");

    /* Submit the block size, target bitrate, and maximum error rate */
    temp = htonl(param->block_size);   if (fwrite(&temp, 4, 1, session->server) < 1) return warn("Could not submit block size");
    temp = htonl(param->target_rate);  if (fwrite(&temp, 4, 1, session->server) < 1) return warn("Could not submit target rate");
    temp = htonl(param->error_rate);   if (fwrite(&temp, 4, 1, session->server) < 1) return warn("Could not submit error rate");
    if (fflush(session->server))
	return warn("Could not flush control channel");

    /* submit the slower and faster factors */
    temp = htons(param->slower_num);  if (fwrite(&temp, 2, 1, session->server) < 1) return warn("Could not submit slowdown numerator");
    temp = htons(param->slower_den);  if (fwrite(&temp, 2, 1, session->server) < 1) return warn("Could not submit slowdown denominator");
    temp = htons(param->faster_num);  if (fwrite(&temp, 2, 1, session->server) < 1) return warn("Could not submit speedup numerator");
    temp = htons(param->faster_den);  if (fwrite(&temp, 2, 1, session->server) < 1) return warn("Could not submit speedup denominator");
    if (fflush(session->server))
	return warn("Could not flush control channel");

    /* populate the fields of the transfer object */
    memset(xfer, 0, sizeof(*xfer));
    xfer->remote_filename = remote_filename;
    xfer->local_filename  = local_filename;

    /* read in the file length, block size, block count, and run epoch */
    if (fread(&xfer->file_size,   8, 1, session->server) < 1) return warn("Could not read file size");         xfer->file_size   = ntohll(xfer->file_size);
    if (fread(&temp,              4, 1, session->server) < 1) return warn("Could not read block size");        if (htonl(temp) != param->block_size) return warn("Block size disagreement");
    if (fread(&xfer->block_count, 4, 1, session->server) < 1) return warn("Could not read number of blocks");  xfer->block_count = ntohl (xfer->block_count);
    if (fread(&xfer->epoch,       4, 1, session->server) < 1) return warn("Could not read run epoch");         xfer->epoch       = ntohl (xfer->epoch);

    /* we start out with every block yet to transfer */
    xfer->blocks_left = xfer->block_count;

    /* try to open the file for writing */
    xfer->file = fopen64(local_filename, "wb");
    if (xfer->file == NULL)
	return warn("Could not open local file for writing");
    
    #ifdef VSIB_REALTIME
    /* try to open the vsib for output */
    xfer->vsib = fopen64("/dev/vsib", "wb");
    if (xfer->vsib == NULL)
    return warn("VSIB board does not exist in /dev/vsib or it cannot be read");
    
    /* pre-reserve the ring buffer */  
    param->ringbuf = malloc(param->block_size * RINGBUF_BLOCKS); 
    if (param->ringbuf == NULL)
    return warn("Could not reserve space for ring buffer");
    #endif
    
    /* if we're doing a transcript */
    if (param->transcript_yn)
	xscript_open(session);

    /* indicate success */
    return 0;
}


/*------------------------------------------------------------------------
 * int ttp_open_port(ttp_session_t *session);
 *
 * Creates a new UDP socket for receiving the file data associated with
 * our pending transfer and communicates the port number back to the
 * server.  Returns 0 on success and non-zero on failure.
 *------------------------------------------------------------------------*/
int ttp_open_port(ttp_session_t *session)
{
    struct sockaddr udp_address;
    int             udp_length = sizeof(udp_address);
    int             status;
    u_int16_t      *port;

    /* open a new datagram socket */
    session->transfer.udp_fd = create_udp_socket(session->parameter);
    if (session->transfer.udp_fd < 0)
	return warn("Could not create UDP socket");

    /* find out the port number we're using */
    memset(&udp_address, 0, sizeof(udp_address));
    getsockname(session->transfer.udp_fd, (struct sockaddr *) &udp_address, &udp_length);

    /* get a hold of the port number */
    port = (session->parameter->ipv6_yn ? &((struct sockaddr_in6 *) &udp_address)->sin6_port : &((struct sockaddr_in *) &udp_address)->sin_port);

    /* send that port number to the server */
    status = fwrite(port, 2, 1, session->server);
    if ((status < 1) || fflush(session->server)) {
	close(session->transfer.udp_fd);
	return warn("Could not send UDP port number");
    }

    /* we succeeded */
    return 0;
}


/*------------------------------------------------------------------------
 * int ttp_repeat_retransmit(ttp_session_t *session);
 *
 * Tries to repeat all of the outstanding retransmit requests for the
 * current transfer on the given session.  Returns 0 on success and
 * non-zero on error.  This also takes care of maintanence operations
 * on the transmission table, such as relocating the entries toward the
 * bottom of the array.
 *------------------------------------------------------------------------*/
int ttp_repeat_retransmit(ttp_session_t *session)
{
    retransmission_t  retransmission[MAX_RETRANSMISSION_BUFFER];  /* the retransmission request object        */
    int               entry;                                      /* an index into the retransmission table   */
    int               status;
    int               block;
    int               count = 0;
    retransmit_t     *rexmit = &(session->transfer.retransmit);

    /* report on the current status */
    //if (session->parameter->verbose_yn) {
    //    fprintf(stderr, "Repeating retransmission requests [%d].\n", rexmit->index_max);      
    //    fprintf(stderr, "Current error rate = %u\n", ntohl(retransmission[0].error_rate));
    //}

    /* if the queue is huge (over MAX_RETRANSMISSION_BUFFER entries) */
    if (rexmit->index_max > MAX_RETRANSMISSION_BUFFER) {
    
        #ifdef DEBUG_RETX
        warn("ttp_repeat_retransmit: MAX_RETRANSMISSION_BUFFER size exceeded");
        #endif
    
        /* prepare a restart-at request, restart from first block (assumes rexmit->table is ordered) */
        retransmission[0].request_type = htons(REQUEST_RESTART);
        retransmission[0].block        = htonl(rexmit->table[0]);
        
        /* send out the request */
        status = fwrite(&retransmission[0], sizeof(retransmission[0]), 1, session->server);
        if ((status <= 0) || fflush(session->server))
            return warn("Could not send restart-at request");
        
        /* reset the retransmission table */
        session->transfer.next_block         = rexmit->table[0];
        session->transfer.stats.total_blocks = rexmit->table[0];
        session->transfer.stats.this_blocks  = rexmit->table[0];
        session->transfer.stats.this_retransmits = MAX_RETRANSMISSION_BUFFER;
        rexmit->index_max                    = 0;
        
        #ifdef DEBUG_RETX
        warn("ttp_repeat_retransmit: REQUEST_RESTART sent and rexmit table cleared");
        #endif

        /* and return */
        return 0;
    }

   /* for each table entry, discard from the table those blocks we don't want, and */ 
   /* prepare a retransmit request */
   session->transfer.stats.this_retransmits = 0;
   for (entry = 0; entry < rexmit->index_max; ++entry) {

      /* get the block number */
      block = rexmit->table[entry];
      
      /* if we want the block */
      if (block && !(session->transfer.received[block / 8] & (1 << (block % 8)))) {
      
         /* save it */
         rexmit->table[count] = block;
         
         /* update the statistics */
         ++(session->transfer.stats.total_retransmits);
         ++(session->transfer.stats.this_retransmits);
         
         /* prepare a retransmit request */
         retransmission[count].request_type = htons(REQUEST_RETRANSMIT);
         retransmission[count].block        = htonl(block);
         #ifdef DEBUG_RETX
         printf("retx %ld\n", block);
         #endif
         ++count;
      }
   }
   rexmit->index_max = count;

   /* send out the requests */
   if (count > 0) {
      status = fwrite(retransmission, sizeof(retransmission_t), count, session->server);
      if (status <= 0) {
         return warn("Could not send retransmit requests");
      }
   }

   /* flush the server connection */
   if (fflush(session->server))
      return warn("Could not clear retransmission buffer");

   /* we succeeded */
   return 0;
}


/*------------------------------------------------------------------------
 * int ttp_request_retransmit(ttp_session_t *session, u_int32_t block);
 *
 * Requests a retransmission of the given block in the current transfer.
 * Returns 0 on success and non-zero otherwise.
 *------------------------------------------------------------------------*/
int ttp_request_retransmit(ttp_session_t *session, u_int32_t block)
{
   #ifdef RETX_REQBLOCK_SORTING
   u_int32_t     tmp32_ins = 0, tmp32_up;
   u_int32_t     idx = 0;
   #endif
   retransmit_t *rexmit = &(session->transfer.retransmit);

   /* if we don't have space for the request */
   if (rexmit->index_max >= rexmit->table_size) {

      /* try to reallocate the table */
      rexmit->table = (u_int32_t *) realloc(rexmit->table, 8 * rexmit->table_size);
      if (rexmit->table == NULL)
         return warn("Could not grow retransmission table");

      /* prepare the new table space */
      memset(rexmit->table + rexmit->table_size, 0, 4 * rexmit->table_size);
      rexmit->table_size *= 2;
   }

   #ifndef RETX_REQBLOCK_SORTING
   /* store the request */
   rexmit->table[(rexmit->index_max)++] = block;
   return 0;
   #else
   /* Store the request via "insertion sort"
      this maintains a sequentially sorted table and discards duplicate requests,
      and does not flood the net with so many unnecessary retransmissions like old Tsunami did
   */
   while ((idx < rexmit->index_max) && (rexmit->table[idx] < block)) idx++; /* seek to insertion point or end */
   if (idx == rexmit->index_max) { /* append at end */
      rexmit->table[(rexmit->index_max)++] = block;
   } else if (rexmit->table[idx] == block) { /* don't insert duplicates */
      // fprintf(stderr, "duplicate retransmit req for block %d discarded\n", block);
   } else { /* insert and shift remaining table upwards */
      tmp32_ins = block;
      do {
         tmp32_up = rexmit->table[idx];
         rexmit->table[idx++] = tmp32_ins;
         tmp32_ins = tmp32_up;
      } while(idx <= rexmit->index_max);
      rexmit->index_max++;
   }
   #endif

   /* we succeeded */
   return 0;
}


/*------------------------------------------------------------------------
 * int ttp_request_stop(ttp_session_t *session);
 *
 * Requests that the server stop transmitting data for the current
 * file transfer in the given session.  This is done by sending a
 * retransmission request with a type of REQUEST_STOP.  Returns 0 on
 * success and non-zero otherwise.  Success means that we successfully
 * requested, not that we successfully halted.
 *------------------------------------------------------------------------*/
int ttp_request_stop(ttp_session_t *session)
{
    retransmission_t retransmission = { 0, 0, 0 };
    int              status;

    /* initialize the retransmission structure */
    retransmission.request_type = htons(REQUEST_STOP);

    /* send out the request */
    status = fwrite(&retransmission, sizeof(retransmission), 1, session->server);
    if ((status <= 0) || fflush(session->server))
    return warn("Could not request end of transmission");
    
    #ifdef VSIB_REALTIME
    /* Wait until ring buffer is empty, then stop vsib and free buffer memory*/
    stop_vsib(session);
    free(session->parameter->ringbuf);
    session->parameter->ringbuf = NULL; /* it is no more... */ 
    #endif

    /* we succeeded */
    return 0;
}


/*------------------------------------------------------------------------
 * int ttp_update_stats(ttp_session_t *session);
 *
 * This routine must be called every interval to update the statistics
 * for the progress of the ongoing file transfer.  Returns 0 on success
 * and non-zero on failure.  (There is not currently any way to fail.)
 *------------------------------------------------------------------------*/
int ttp_update_stats(ttp_session_t *session)
{
    time_t            now_epoch = time(NULL);                 /* the current Unix epoch                         */
    u_int64_t         delta;                                  /* the data transferred since last stats          */
    u_int64_t         delta_total;                            /* the total data transferred since last stats    */
    u_int64_t         delta_useful;                           /* the real trhoughput since last stats           */
    u_int64_t         temp;                                   /* temporary value for building the elapsed time  */
    int               hours, minutes, seconds, milliseconds;  /* parts of the elapsed time                      */
    u_int64_t         data_total;                             /* the total amount of data transferred           */
    u_int64_t         data_last;                              /* the total amount of data since last stat time  */
    statistics_t     *stats = &(session->transfer.stats);
    retransmission_t  retransmission;
    int               status;
    static u_int32_t  iteration = 0;
    static char       stats_line[128];

    /* find the total time elapsed */
    delta        =        get_usec_since(&stats->this_time);
    delta_total  = temp = get_usec_since(&stats->start_time);
    milliseconds = (temp % 1000000) / 1000;  temp /= 1000000;
    seconds      = temp % 60;                temp /= 60;
    minutes      = temp % 60;                temp /= 60;
    hours        = temp;

    /* find the amount of data transferred */
    data_total = ((u_int64_t) session->parameter->block_size) *  stats->total_blocks;
    data_last  = ((u_int64_t) session->parameter->block_size) * (stats->total_blocks - stats->this_blocks);
    delta_useful = data_last - (stats->this_retransmits) * ((u_int64_t) session->parameter->block_size);

    /* get the current UDP receive error count reported by the operating system */    
    stats->this_udp_errors = get_udp_in_errors();
    
    /* update the rate statistics */
    stats->transmit_rate   = 0.01 * ((session->parameter->history * stats->transmit_rate)   + ((100 - session->parameter->history) * data_last * 8.0 / delta));
    stats->retransmit_rate = session->parameter->history          * (0.01 * stats->retransmit_rate) +
    	                     (100 - session->parameter->history)  * (0.50 * 1000 * (stats->this_retransmits / (1.0 + stats->this_retransmits + stats->total_blocks - stats->this_blocks)) +
	                                                             0.50 * 1000 * session->transfer.ring_buffer->count_data / MAX_BLOCKS_QUEUED);

    /* send along the current error rate information */
    retransmission.request_type = htons(REQUEST_ERROR_RATE);
    retransmission.error_rate   = htonl(session->transfer.stats.retransmit_rate);
    status = fwrite(&retransmission, sizeof(retransmission), 1, session->server);
    if ((status <= 0) || fflush(session->server))
	return warn("Could not send error rate information");

    /* build the stats string */    
#ifdef STATS_MATLABFORMAT
    sprintf(stats_line, "%02d\t%02d\t%02d\t%03d\t%4u\t%6.2f\t%6.1f\t%5.1f\t%7u\t%6.1f\t%6.1f\t%5.1f\t%5d\t%5d\t%7u\t%8u\t%8lld\n",
#else
    sprintf(stats_line, "%02d:%02d:%02d.%03d %4u %6.2fM %6.1fMbps %5.1f%% %7u %6.1fG %6.1fMbps %5.1f%% %5d %5d %7u %8u %8lld\n",
#endif
	    hours, minutes, seconds, milliseconds,
	    stats->total_blocks - stats->this_blocks,
	    data_last / (1024.0 * 1024.0),
	    (data_last * 8.0 / delta),
	    100.0 * stats->this_retransmits / (1.0 + stats->this_retransmits + stats->total_blocks - stats->this_blocks),
	    session->transfer.stats.total_blocks,
	    data_total / (1024.0 * 1024.0 * 1024.0),
	    (data_total * 8.0 / delta_total),
	    100.0 * stats->total_retransmits / (stats->total_retransmits + stats->total_blocks),
	    session->transfer.retransmit.index_max,
	    session->transfer.ring_buffer->count_data,
        //delta_useful * 8.0 / delta,
        session->transfer.blocks_left, 
        stats->this_retransmits, // NOTE: stats->this_retransmits seems to be 0 always ??
        stats->this_udp_errors - stats->start_udp_errors
        );

    /* give the user a show if they want it */
    if (session->parameter->verbose_yn) {

	/* screen mode */
	if (session->parameter->output_mode == SCREEN_MODE) {
	    printf("\033[2J\033[H");
	    printf("Current time:   %s\n", ctime(&now_epoch));
	    printf("Elapsed time:   %02d:%02d:%02d.%03d\n\n", hours, minutes, seconds, milliseconds);
	    printf("Last interval\n--------------------------------------------------\n");
	    printf("Blocks count:     %u\n",             stats->total_blocks - stats->this_blocks);
	    printf("Data transferred: %0.2f GB\n",       data_last  / (1024.0 * 1024.0 * 1024.0));
	    printf("Transfer rate:    %0.2f Mbps\n",     (data_last  * 8.0 / delta));
	    printf("Retransmissions:  %u (%0.2f%%)\n\n", stats->this_retransmits,  (100.0 * stats->this_retransmits / (stats->total_blocks - stats->this_blocks)));
	    printf("Cumulative\n--------------------------------------------------\n");
	    printf("Blocks count:     %u\n",             session->transfer.stats.total_blocks);
	    printf("Data transferred: %0.2f GB\n",       data_total / (1024.0 * 1024.0 * 1024.0));
	    printf("Transfer rate:    %0.2f Mbps\n",     (data_total * 8.0 / delta_total));
	    printf("Retransmissions:  %u (%0.2f%%)\n\n", stats->total_retransmits, (100.0 * stats->total_retransmits / stats->total_blocks));
        printf("OS UDP rx errors: %lld\n",             stats->this_udp_errors - stats->start_udp_errors);

	/* line mode */
	} else {

	    /* print a header if necessary */
#ifndef STATS_NOHEADER
	    if (!(iteration++ % 23)) {
		printf("             last_interval                   transfer_total                   buffers      transfer_remaining  OS UDP\n");
		printf("time          blk    data       rate rexmit     blk    data       rate rexmit queue  ring     blk   rt_len      err \n");
	    }
#endif
	    printf("%s", stats_line);
	}

	/* and flush the output */
	fflush(stdout);
    }

    /* print to the transcript if the user wants */
    if (session->parameter->transcript_yn)
	xscript_data_log(session, stats_line);

    /* clear out the statistics again */
    stats->this_blocks      = stats->total_blocks;
    stats->this_retransmits = 0;
    gettimeofday(&(stats->this_time), NULL);

    /* indicate success */
    return 0;
}


/*========================================================================
 * $Log$
 * Revision 1.11  2006/12/05 15:24:50  jwagnerhki
 * now noretransmit code in client only, merged rt client code
 *
 * Revision 1.10  2006/11/10 11:29:45  jwagnerhki
 * updated stats display
 *
 * Revision 1.9  2006/10/28 19:29:15  jwagnerhki
 * jamil GET* merge, insertionsort disabled by default again
 *
 * Revision 1.8  2006/10/27 20:12:36  jwagnerhki
 * fix for very bad original retransmit req assembly
 *
 * Revision 1.7  2006/10/25 14:53:16  jwagnerhki
 * removed superfluous ACK on GET*
 *
 * Revision 1.6  2006/10/25 14:20:31  jwagnerhki
 * attempt to support multimode get already implemented in Jamil server
 *
 * Revision 1.5  2006/10/19 08:06:31  jwagnerhki
 * fix STATS_MATLABFORMAT ifndef to ifdef
 *
 * Revision 1.4  2006/10/17 12:39:50  jwagnerhki
 * disabled retransmit debug infos on default
 *
 * Revision 1.3  2006/08/08 08:38:20  jwagnerhki
 * added some debug output trying to catch file corruption issues
 *
 * Revision 1.2  2006/07/21 08:50:41  jwagnerhki
 * merged client and rtclient protocol.c
 *
 * Revision 1.1.1.1  2006/07/20 09:21:19  jwagnerhki
 * reimport
 *
 * Revision 1.2  2006/07/11 07:38:32  jwagnerhki
 * new debug defines
 *
 * Revision 1.1  2006/07/10 12:26:51  jwagnerhki
 * deleted unnecessary files
 *
 */
