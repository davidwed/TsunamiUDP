/*========================================================================
 * protocol.c  --  TTP routines for Tsunami client.
 *
 * This contains the Tsunami Transfer Protocol API for the Tsunami
 * file transfer client.
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

#include <stdlib.h>       /* for *alloc() and free()               */
#include <string.h>       /* for standard string routines          */
#include <sys/socket.h>   /* for the BSD socket library            */
#include <sys/time.h>     /* for gettimeofday()                    */
#include <time.h>         /* for time()                            */
#include <unistd.h>       /* for standard Unix system calls        */

#include <tsunami-client.h>

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
    status = fwrite(&client_revision, sizeof(client_revision), 1, session->server);
    if ((status < 1) || fflush(session->server))
        return warn("Could not send protocol revision number");

    /* read the protocol revision number from the server */
    status = fread(&server_revision, sizeof(server_revision), 1, session->server);
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
    int              status;
    ttp_transfer_t  *xfer  = &session->transfer;
    ttp_parameter_t *param =  session->parameter;
    ttp_parameter_t  outparam;

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
    memcpy(&outparam, param, sizeof(outparam));
    host_to_net(outparam.block_size);
    host_to_net(outparam.target_rate);
    host_to_net(outparam.error_rate);
    if (fwrite_from(session->server, outparam.block_size) < 1)  return warn("Could not submit block size");
    if (fwrite_from(session->server, outparam.target_rate) < 1) return warn("Could not submit target rate");
    if (fwrite_from(session->server, outparam.error_rate) < 1)  return warn("Could not submit error rate");
    if (fflush(session->server))
        return warn("Could not flush control channel");

    /* submit the slower and faster factors */
    host_to_net(outparam.slower_num);
    host_to_net(outparam.slower_den);
    host_to_net(outparam.faster_num);
    host_to_net(outparam.faster_den);
    if (fwrite_from(session->server, outparam.slower_num) < 1) return warn("Could not submit slowdown numerator");
    if (fwrite_from(session->server, outparam.slower_den) < 1) return warn("Could not submit slowdown denominator");
    if (fwrite_from(session->server, outparam.faster_num) < 1) return warn("Could not submit speedup numerator");
    if (fwrite_from(session->server, outparam.faster_den) < 1) return warn("Could not submit speedup denominator");
    if (fflush(session->server))
        return warn("Could not flush control channel");

    /* populate the fields of the transfer object */
    memset(xfer, 0, sizeof(*xfer));
    xfer->remote_filename = remote_filename;
    xfer->local_filename  = local_filename;

    /* read in the file length, block size, block count, and run epoch */
    if (fread_into(session->server, xfer->file_size) < 1)       return warn("Could not read file size");
    if (fread_into(session->server, outparam.block_size) < 1)   return warn("Could not read block size");
    if (fread_into(session->server, xfer->block_count) < 1)     return warn("Could not read number of blocks");
    if (fread_into(session->server, xfer->epoch) < 1)           return warn("Could not read run epoch");
    net_to_host(xfer->file_size);
    net_to_host(outparam.block_size); // value is discarded later
    net_to_host(xfer->block_count);
    net_to_host(xfer->epoch);

    /* double-check that the server and we are using the same blocksize setting */
    if (outparam.block_size != param->block_size)
        return warn("Block size disagreement");

    /* we start out with every block yet to transfer */
    xfer->blocks_left = xfer->block_count;

    /* check if the local file will be overwritten */
    if (!access(xfer->local_filename, F_OK))
        printf("Warning: overwriting existing file '%s'\n", local_filename);

    /* open the local file for writing */
    xfer->file = fopen64(xfer->local_filename, "wb");
    if (xfer->file == NULL) {
        /* if filename contained full path info, try again with just the name */
        char * trimmed = rindex(xfer->local_filename, '/');
        if ((trimmed != NULL) && (strlen(trimmed)>1)) {
            printf("Warning: could not open file %s for writing, trying local directory instead.\n", xfer->local_filename);
            xfer->local_filename = trimmed + 1;
            if (!access(xfer->local_filename, F_OK))
                printf("Warning: overwriting existing file '%s'\n", xfer->local_filename);
            xfer->file = fopen64(xfer->local_filename, "wb");
        }
        if(xfer->file == NULL) {
           return warn("Could not open local file for writing");
        }
    }

    #ifdef VSIB_REALTIME
    /* try to open the vsib for output */
    xfer->vsib = fopen64("/dev/vsib", "wb");
    if (xfer->vsib == NULL)
        return warn("VSIB board does not exist in /dev/vsib or it cannot be read");
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
    socklen_t       udp_length = sizeof(udp_address);
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
    port = (session->parameter->ipv6_yn ?
                &((struct sockaddr_in6 *) &udp_address)->sin6_port   :
                &((struct sockaddr_in *) &udp_address)->sin_port);

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
    u_int32_t         entry;                                      /* an index into the retransmission table   */
    size_t            status;
    u_int64_t         block;
    u_int32_t         count = 0;
    retransmit_t     *rexmit = &(session->transfer.retransmit);

    /* report on the current status */
    //if (session->parameter->verbose_yn) {
    //    fprintf(stderr, "Repeating retransmission requests [%d].\n", rexmit->index_max);      
    //    fprintf(stderr, "Current error rate = %u\n", ntohl(retransmission[0].error_rate));
    //}

    /* skip blanks (e.g. in semi-lossy, or completely lossless transfer) */
    if (rexmit->index_max == 0) return 0;

    /* to keep Valgrind happy */
    memset(retransmission, 0, sizeof(retransmission));

    /* if the queue is huge (over MAX_RETRANSMISSION_BUFFER entries) */
    if (rexmit->index_max > MAX_RETRANSMISSION_BUFFER) {

        /* prepare a restart-at request, restart from first block (assumes rexmit->table is ordered) */
        retransmission[0].request_type = REQUEST_RESTART;
        retransmission[0].block        = rexmit->table[0];
        host_to_net(retransmission[0].request_type);
        host_to_net(retransmission[0].block);

        /* send out the request */
        status = fwrite(&retransmission[0], sizeof(retransmission[0]), 1, session->server);
        if ((status <= 0) || fflush(session->server))
            return warn("Could not send restart-at request");

        /* remember the request was sent - we can then discard blocks that are still on the line */
        session->transfer.restart_pending    = 1;
        session->transfer.restart_lastidx    = rexmit->table[rexmit->index_max - 1];

        /* reset the retransmission table */
        session->transfer.next_block             = rexmit->table[0];
        session->transfer.stats.total_blocks     = rexmit->table[0];
        session->transfer.stats.this_blocks      = rexmit->table[0];
        session->transfer.stats.this_retransmits = MAX_RETRANSMISSION_BUFFER;
        rexmit->index_max                        = 0;

        /* and return */
        return 0;
    }

    /* for each table entry, discard from the table those blocks we don't want, and */
    /* prepare a retransmit request */
    count = 0;
    for (entry = 0; entry < rexmit->index_max; ++entry) {

        /* get the block number */
        block = rexmit->table[entry];

        /* if we want the block */
        if (block && !(session->transfer.received[block / 8] & (1 << (block % 8)))) {

            /* save it */
            rexmit->table[count] = block;

            /* prepare a retransmit request */
            retransmission[count].request_type = REQUEST_RETRANSMIT;
            retransmission[count].block        = block;
            host_to_net(retransmission[count].request_type);
            host_to_net(retransmission[count].block);

            ++count;
        }
    }

    /* update the statistics */
    session->transfer.stats.total_retransmits += count;
    session->transfer.stats.this_retransmits   = count;

    /* send out the requests */
    rexmit->index_max = count;
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
int ttp_request_retransmit(ttp_session_t *session, u_int64_t block)
{
    #ifdef RETX_REQBLOCK_SORTING
    u_int64_t     tmp64_ins = 0, tmp64_up;
    u_int64_t     idx = 0;
    #endif
    u_int64_t    *ptr;
    retransmit_t *rexmit = &(session->transfer.retransmit);

    /* double checking: if we already got the block, don't add it */
    if (session->transfer.received[block / 8] & (1 << (block % 8)))
        return 0;

    /* if we don't have space for the request */
    if (rexmit->index_max >= rexmit->table_size) {

        /* try to reallocate the table */
        ptr = (u_int64_t *) realloc(rexmit->table, 2 * sizeof(u_int64_t) * rexmit->table_size);
        if (ptr == NULL)
            return warn("Could not grow retransmission table");

        /* prepare the new table space */
        rexmit->table = ptr;
        memset(rexmit->table + rexmit->table_size, 0, sizeof(u_int64_t) * rexmit->table_size);
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
        tmp64_ins = block;
        do {
            tmp64_up = rexmit->table[idx];
            rexmit->table[idx++] = tmp64_ins;
            tmp64_ins = tmp64_up;
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
    retransmission.request_type = REQUEST_STOP;
    host_to_net(retransmission.request_type);

    /* send out the request */
    status = fwrite(&retransmission, sizeof(retransmission), 1, session->server);
    if ((status <= 0) || fflush(session->server))
        return warn("Could not request end of transmission");

    #ifdef VSIB_REALTIME
    /* stop the VSIB (func will wait until all pending data DMA'ed out) */
    stop_vsib(session);
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
    u_int64_t         delta;                                  /* time delta since last statistics update (usec) */
    u_int64_t         delta_total;                            /* tiem delta since start of transmission (usec)  */
    u_int64_t         temp;                                   /* temporary value for building the elapsed time  */
    int               hours, minutes, seconds, milliseconds;  /* parts of the elapsed time                      */
    u_int64_t         data_total;                             /* the total amount of data transferred (bytes)   */
    u_int64_t         data_last;                              /* the total amount of data since last stat time  */
    u_int64_t         data_useful;                            /* the real trhoughput since last stats           */
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
    data_total  = ((u_int64_t) session->parameter->block_size) *  stats->total_blocks;
    data_last   = ((u_int64_t) session->parameter->block_size) * (stats->total_blocks - stats->this_blocks);
    data_useful = data_last - (stats->this_retransmits) * ((u_int64_t) session->parameter->block_size);

    /* get the current UDP receive error count reported by the operating system */
    stats->this_udp_errors = get_udp_in_errors();

    /* update the rate statistics */
    // transmit rate R = useful R (Mbit/s) + retransmit R (Mbit/s)
    stats->this_transmit_rate   = data_last * 8.0 / delta; 
    stats->this_retransmit_rate = (data_last - data_useful) * 8.0 / delta;
    // IIRfilter(rate R) = (1.0 - h) * current_rate  +  h * old_rate 
    stats->transmit_rate        = 0.01 * ( 
        (100 - session->parameter->history) * stats->this_transmit_rate
      + (session->parameter->history * stats->transmit_rate) );
    // IIR filtered composite error and loss, some sort of knee function
    stats->error_rate = session->parameter->history * (0.01 * stats->error_rate) +
         (100 - session->parameter->history)
          * 0.50 * 1000 
          * ( (stats->this_retransmits / (1.0 + stats->this_retransmits + stats->total_blocks - stats->this_blocks)) 
               + session->transfer.ring_buffer->count_data / MAX_BLOCKS_QUEUED );

    /* send the current error rate information to the server */
    memset(&retransmission, 0, sizeof(retransmission));
    retransmission.request_type = htons(REQUEST_ERROR_RATE);
    retransmission.error_rate   = htonl((u_int64_t) session->transfer.stats.error_rate);
    status = fwrite(&retransmission, sizeof(retransmission), 1, session->server);
    if ((status <= 0) || fflush(session->server))
        return warn("Could not send error rate information");

    /* build the stats string */    
    #ifdef STATS_MATLABFORMAT
    sprintf(stats_line, "%02d\t%02d\t%02d\t%03d\t%6Lu\t%6.2f\t%6.1f\t%5.1f\t%7Lu\t%6.1f\t%6.1f\t%5.1f\t%5Lu\t%5u\t%8Lu\t%8u\t%8Lu\n",
    #else
    sprintf(stats_line, "%02d:%02d:%02d.%03d %6Lu %6.2fM %6.1fMbps %5.1f%% %7Lu %6.1fG %6.1fMbps %5.1f%% %5Lu %5u %8Lu %8u %8Lu\n",
    #endif
        hours, minutes, seconds, milliseconds,
        (ull_t)(stats->total_blocks - stats->this_blocks),
        stats->this_retransmit_rate,
        stats->this_transmit_rate,
        100.0 * stats->this_retransmits / (1.0 + stats->this_retransmits + stats->total_blocks - stats->this_blocks),
        (ull_t)session->transfer.stats.total_blocks,
        data_total / (1024.0 * 1024.0 * 1024.0),
        (data_total * 8.0 / delta_total),
        100.0 * stats->total_retransmits / (stats->total_retransmits + stats->total_blocks),
        (ull_t)session->transfer.retransmit.index_max,
        session->transfer.ring_buffer->count_data,
        //delta_useful * 8.0 / delta,
        (ull_t)session->transfer.blocks_left,
        stats->this_retransmits, // NOTE: stats->this_retransmits seems to be 0 always ??
        (ull_t)(stats->this_udp_errors - stats->start_udp_errors)
        );

    /* give the user a show if they want it */
    if (session->parameter->verbose_yn) {

        /* screen mode */
        if (session->parameter->output_mode == SCREEN_MODE) {
            printf("\033[2J\033[H");
            printf("Current time:   %s\n", ctime(&now_epoch));
            printf("Elapsed time:   %02d:%02d:%02d.%03d\n\n", hours, minutes, seconds, milliseconds);
            printf("Last interval\n--------------------------------------------------\n");
            printf("Blocks count:     %Lu\n",            (ull_t)(stats->total_blocks - stats->this_blocks));
            printf("Data transferred: %0.2f GB\n",       data_last  / (1024.0 * 1024.0 * 1024.0));
            printf("Transfer rate:    %0.2f Mbps\n",     (data_last  * 8.0 / delta));
            printf("Retransmissions:  %u (%0.2f%%)\n\n", stats->this_retransmits,  (100.0 * stats->this_retransmits / (stats->total_blocks - stats->this_blocks)));
            printf("Cumulative\n--------------------------------------------------\n");
            printf("Blocks count:     %Lu\n" ,           (ull_t)(session->transfer.stats.total_blocks));
            printf("Data transferred: %0.2f GB\n",       data_total / (1024.0 * 1024.0 * 1024.0));
            printf("Transfer rate:    %0.2f Mbps\n",     (data_total * 8.0 / delta_total));
            printf("Retransmissions:  %u (%0.2f%%)\n\n", stats->total_retransmits, (100.0 * stats->total_retransmits / stats->total_blocks));
            printf("OS UDP rx errors: %Lu\n" ,           (ull_t)(stats->this_udp_errors - stats->start_udp_errors));

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
 * Revision 1.21.2.5  2007/11/12 14:20:59  jwagnerhki
 * removed some printf warnings on x64 platforms
 *
 * Revision 1.21.2.4  2007/11/10 14:49:24  jwagnerhki
 * first try at 64-bit 'clean' compile
 *
 * Revision 1.21.2.3  2007/11/10 09:58:11  jwagnerhki
 * more host to nets
 *
 * Revision 1.21.2.2  2007/11/10 09:46:21  jwagnerhki
 * int to u_int64_t
 *
 * Revision 1.21.2.1  2007/11/09 22:43:51  jwagnerhki
 * protocol v1.2 build 1
 *
 *
 */
