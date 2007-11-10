/*========================================================================
 * protocol.c  --  TTP routines for Tsunami server.
 *
 * This contains the Tsunami Transfer Protocol API for the Tsunami file
 * transfer server.
 *
 * Written by Mark Meiss (mmeiss@indiana.edu).
 * Copyright (C) 2002 The Trustees of Indiana University.
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

#include <string.h>      /* for memset(), strdup(), etc.   */
#include <sys/types.h>   /* for standard system data types */
#include <sys/socket.h>  /* for the BSD sockets library    */
#include <sys/time.h>    /* gettimeofday()                 */
#include <time.h>        /* for time()                     */
#include <unistd.h>      /* for standard Unix system calls */
#include <assert.h>
#include <math.h>        /* floor() */


#include <tsunami-server.h>

#ifdef VSIB_REALTIME
#include "parse_evn_filename.h" /* EVN file name parsing for start time, station code, etc */
#endif


/*------------------------------------------------------------------------
 * int ttp_accept_retransmit(ttp_session_t *session,
 *                           retransmission_t *retransmission,
 *                           u_char *datagram);
 *
 * Handles the given retransmission request.  The actions taken depend
 * on the nature of the request:
 *
 *   REQUEST_RETRANSMIT -- Retransmit the given block.
 *   REQUEST_RESTART    -- Restart the transfer at the given block.
 *   REQUEST_ERROR_RATE -- Use the given error rate to adjust the IPD.
 *
 * For REQUEST_RETRANSMIT messsages, the given buffer must be large
 * enough to hold (block_size + |blockheader_t|) bytes.  For other messages,
 * the datagram parameter is ignored.
 *
 * Returns 0 on success and non-zero on failure.
 *------------------------------------------------------------------------*/
int ttp_accept_retransmit(ttp_session_t *session, retransmission_t *retransmission, u_char *datagram)
{
    ttp_transfer_t  *xfer      = &session->transfer;
    ttp_parameter_t *param     = session->parameter;
    static int       iteration = 0;
    static char      stats_line[80];
    int              status;

    /* convert the retransmission fields to host byte order */
    net_to_host(retransmission->block);
    net_to_host(retransmission->error_rate);
    net_to_host(retransmission->request_type);

    /* handle error rate notification */
    if (REQUEST_ERROR_RATE == retransmission->request_type) {

        /* calculate a new IPD */
        if (retransmission->error_rate > param->error_rate) {
            double factor1 = (1.0 * param->slower_num / param->slower_den) - 1.0;
            double factor2 = (1.0 + retransmission->error_rate - param->error_rate) / (100000.0 - param->error_rate);
            xfer->ipd_current *= 1.0 + (factor1 * factor2);
        } else {
            xfer->ipd_current = (u_int32_t) (xfer->ipd_current * (u_int64_t) param->faster_num / param->faster_den);
        }

        /* build the stats string */
        sprintf(stats_line, "%6u %5uus %5uus %7Lu %6.2f%% %3u\n",
            retransmission->error_rate, xfer->ipd_current, param->ipd_time, (ull_t)xfer->block,
            100.0 * xfer->block / param->block_count, session->session_id);

        /* make sure the IPD is still in range, for later calculations */
        xfer->ipd_current = max(min(xfer->ipd_current, 10000), param->ipd_time);

        /* print a status report */
        if (!(iteration++ % 23))
            printf(" erate     ipd  target   block   %%done srvNr\n");

        printf(stats_line);

        /* print to the transcript if the user wants */
        if (param->transcript_yn)
            xscript_data_log(session, stats_line);

    /* handle request to restart from block X */
    } else if (REQUEST_RESTART == retransmission->request_type) {

        /* do range-checking first */
        if ((retransmission->block == 0) || (retransmission->block > param->block_count)) {
            sprintf(g_error, "Attempt to restart at illegal block %Lu", (ull_t)retransmission->block);
            return warn(g_error);
        }

        xfer->block = retransmission->block;

    /* handle request to retransmit at block */
    } else if (REQUEST_RETRANSMIT == retransmission->request_type) {

        /* build the retransmission */
        status = build_datagram(session, retransmission->block, TS_BLOCK_RETRANSMISSION, datagram);
        if (status < 0) {
            sprintf(g_error, "Could not build retransmission for block %Lu", (ull_t)retransmission->block);
            return warn(g_error);
        }

        /* try to send out the block */
        status = sendto(xfer->udp_fd, datagram,
                        sizeof(blockheader_t) + param->block_size, 0,
                        xfer->udp_address,
                        xfer->udp_length);
        if (status < 0) {
            sprintf(g_error, "Could not retransmit block %Lu", (ull_t)retransmission->block);
            return warn(g_error);
        }

    /* handle unknown request */
    } else {

        sprintf(g_error, "Received unknown retransmission request of type %u", retransmission->request_type);
        return warn(g_error);

    }

    /* we're done */
    return 0;
}


/*------------------------------------------------------------------------
 * int ttp_authenticate(ttp_session_t *session, const u_char *secret);
 *
 * Given an active Tsunami session, returns 0 if we are able to
 * negotiate authentication successfully and a non-zero value
 * otherwise.
 *
 * The negotiation process works like this:
 *
 *     (1) The server [this process] sends 512 bits of random data
 *         to the client.
 *
 *     (2) The client XORs 512 bits of the shared secret onto this
 *         random data and responds with the MD5 hash of the result.
 *
 *     (3) The server does the same thing and compares the result.
 *         If the authentication succeeds, the server transmits a
 *         result byte of 0.  Otherwise, it transmits a non-zero
 *         result byte.
 *------------------------------------------------------------------------*/
int ttp_authenticate(ttp_session_t *session, const u_char *secret)
{
    u_char random[64];         /* the buffer of random data               */
    u_char server_digest[16];  /* the MD5 message digest (for us)         */
    u_char client_digest[16];  /* the MD5 message digest (for the client) */
    int    i;
    int    status;

    /* obtain the random data */
    status = get_random_data(random, 64);
    if (status < 0)
        return warn("Access to random data is broken");

    /* send the random data to the client */
    status = write(session->client_fd, random, 64);
    if (status < 0)
        return warn("Could not send authentication challenge to client");

    /* read the results back from the client */
    status = read(session->client_fd, client_digest, 16);
    if (status < 0)
        return warn("Could not read authentication response from client");

    /* compare the two digests */
    prepare_proof(random, 64, secret, server_digest);
    for (i = 0; i < 16; ++i)
    if (client_digest[i] != server_digest[i]) {
        write(session->client_fd, "\001", 1);
        return warn("Authentication failed");
    }

    /* try to tell the client it worked */
    status = write(session->client_fd, "\000", 1);
    if (status < 0)
        return warn("Could not send authentication confirmation to client");

    /* we succeeded */
    return 0;
}


/*------------------------------------------------------------------------
 * int ttp_negotiate(ttp_session_t *session);
 *
 * Performs all of the negotiation with the client that is done prior
 * to authentication.  At the moment, this consists of verifying
 * identical protocol revisions between the client and server.  Returns
 * 0 on success and non-zero on failure.
 *
 * Values are transmitted in network byte order.
 *------------------------------------------------------------------------*/
int ttp_negotiate(ttp_session_t *session)
{
    u_int32_t server_revision = htonl(PROTOCOL_REVISION);
    u_int32_t client_revision;
    int       status;

    /* send our protocol revision number to the client */
    status = write(session->client_fd, &server_revision, 4);
    if (status < 0)
        return warn("Could not send protocol revision number");

    /* read the protocol revision number from the client */
    status = read(session->client_fd, &client_revision, 4);
    if (status < 0)
        return warn("Could not read protocol revision number");

    /* compare the numbers */
    return (client_revision == server_revision) ? 0 : -1;
}


/*------------------------------------------------------------------------
 * int ttp_open_port(ttp_session_t *session);
 *
 * Creates a new UDP socket for transmitting the file data associated
 * with our pending transfer and receives the destination port number
 * from the client.  Returns 0 on success and non-zero on failure.
 *------------------------------------------------------------------------*/
int ttp_open_port(ttp_session_t *session)
{
    struct sockaddr    *address;
    int                 status;
    u_int16_t           port;
    u_char              ipv6_yn = session->parameter->ipv6_yn;

    /* create the address structure */
    session->transfer.udp_length = ipv6_yn ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
    address = (struct sockaddr *) malloc(session->transfer.udp_length);
    if (address == NULL)
        error("Could not allocate space for UDP socket address");

    /* prepare the UDP address structure, minus the UDP port number */
    getpeername(session->client_fd, address, &(session->transfer.udp_length));

    /* read in the port number from the client */
    status = read(session->client_fd, &port, 2);
    if (status < 0)
        return warn("Could not read UDP port number");
    if (ipv6_yn)
        ((struct sockaddr_in6 *) address)->sin6_port = port;
    else
        ((struct sockaddr_in *)  address)->sin_port  = port; // note: sin_port is always in net byte order

    /* print out the port number */
    if (session->parameter->verbose_yn)
        printf("Sending to client port %d\n", ntohs(port));

    /* open a new datagram socket */
    session->transfer.udp_fd = create_udp_socket(session->parameter);
    if (session->transfer.udp_fd < 0)
        return warn("Could not create UDP socket");

    /* we succeeded */
    session->transfer.udp_address = address;
    return 0;
}


/*------------------------------------------------------------------------
 * int ttp_open_transfer(ttp_session_t *session);
 *
 * Tries to create a new TTP file request object for the given session
 * by reading the name of a requested file from the client.  If we are
 * able to negotiate the transfer successfully, we return 0.  If we
 * can't negotiate the transfer because of I/O or file errors, we
 * return a negative vlaue.
 *
 * The client is sent a result byte of 0 if the request is accepted
 * (because the file can be read) and a non-zero result byte otherwise.
 *------------------------------------------------------------------------*/
int ttp_open_transfer(ttp_session_t *session)
{
    char             filename[MAX_FILENAME_LENGTH];  /* the name of the file to transfer     */
    u_int64_t        file_size;                      /* network-order version of file size   */
    u_int32_t        block_size;                     /* network-order version of block size  */
    u_int64_t        block_count;                    /* network-order version of block count */
    time_t           epoch;
    int              status;
    ttp_transfer_t  *xfer  = &session->transfer;
    ttp_parameter_t *param =  session->parameter;
    char            size[10];                        /* for Jamil's GET* hack               */
    char            file_no[10];
    char            message[20];
    int             i;
    struct          timeval ping_s, ping_e;

    #ifdef VSIB_REALTIME
    unsigned long long  ull;                         /* helper for sscanf()                  */
    struct evn_filename *ef;                         /* EVN filename information struct      */
    double              starttime;                   /* start time in usec, gettimeofday()   */
    struct timeval      d;
    #endif

    /* clear out the transfer data */
    memset(xfer, 0, sizeof(*xfer));

    /* read in the requested filename */
    status = read_line(session->client_fd, filename, MAX_FILENAME_LENGTH);
    if (status < 0)
        error("Could not read filename from client");
    filename[MAX_FILENAME_LENGTH - 1] = '\0';

    if(!strcmp(filename, TS_DIRLIST_HACK_CMD)) {

        /* The client requested listing of files and their sizes (dir command)
        * Send strings:   NNN \0   name1 \0 len1 \0     nameN \0 lenN \0
        */
        snprintf(file_no, sizeof(file_no), "%u", param->total_files);
        write(session->client_fd, file_no, strlen(file_no)+1);
        for(i=0; i<param->total_files; i++) {
            write(session->client_fd, param->file_names[i], strlen(param->file_names[i])+1);
            snprintf(message, sizeof(message), "%d", param->file_sizes[i]);
            write(session->client_fd, message, strlen(message)+1);
        }
        read(session->client_fd, message, 1);
        return warn("File list sent!");

    } else if(!strcmp(filename,"*")) {

        /* A multiple file request - sent the file names first, 
        * and next the client requests a download of each in turn (get * command)
        */
        memset(size, 0, sizeof(size));
        snprintf(size, sizeof(size), "%u", param->file_name_size);
        write(session->client_fd, size, 10);

        memset(file_no, 0, sizeof(file_no));
        snprintf(file_no, sizeof(file_no), "%u", param->total_files);
        write(session->client_fd, file_no, 10);

        printf("\nSent multi-GET filename count and array size to client\n");
        read(session->client_fd, message, 8);
        printf("Client response: %s\n", message);

        for(i=0; i<param->total_files; i++)
            write(session->client_fd, param->file_names[i], strlen(param->file_names[i])+1);

        read(session->client_fd, message, 8);
        printf("Sent file list, client response: %s\n", message);

        status = read_line(session->client_fd, filename, MAX_FILENAME_LENGTH);

        if (status < 0)
            error("Could not read filename from client");

    }

    /* store the filename in the transfer object */
    xfer->filename = strdup(filename);
    if (xfer->filename == NULL)
        return warn("Memory allocation error");

    /* make a note of the request */
    if (param->verbose_yn)
        printf("Request for file: '%s'\n", filename);

    #ifndef VSIB_REALTIME

    /* try to open the file for reading */
    xfer->file = fopen64(filename, "r");
    if (xfer->file == NULL) {
        sprintf(g_error, "File '%s' does not exist or cannot be read", filename);
        /* signal failure to the client */
        status = write(session->client_fd, "\x008", 1);
        if (status < 0)
            warn("Could not signal request failure to client");
        return warn(g_error);
    }

    #else

    /* get starting time (UTC) and detect whether local disk copy is wanted */
    if (strrchr(filename,'/') == NULL) {
        ef = parse_evn_filename(filename);          /* attempt to parse */
        param->fileout = 0;
    } else {
        ef = parse_evn_filename(strrchr(filename, '/')+1);       /* attempt to parse */
        param->fileout = 1;
    }
    if (!ef->valid) {
        fprintf(stderr, "Warning: EVN filename parsing failed, '%s' not following EVN File Naming Convention?\n", filename);
    }

    /* try to open the vsib for reading */
    xfer->vsib = fopen64("/dev/vsib", "r");
    if (xfer->vsib == NULL) {
        sprintf(g_error, "VSIB board does not exist in /dev/vsib or it cannot be read");
        status = write(session->client_fd, "\002", 1);
        if (status < 0) {
            warn("Could not signal request failure to client");
        }
        return warn(g_error);
    }

    /* try to open the local disk copy file for writing */
    if (param->fileout) {
        xfer->file = fopen64(filename, "wb");
        if (xfer->file == NULL) {
            sprintf(g_error, "Could not open local file '%s' for writing", filename);
            status = write(session->client_fd, "\x010", 1);
            if (status < 0) {
                warn("Could not signal request failure to client");
            }
            fclose(xfer->vsib);
            return warn(g_error);
        }
    }

    /* Start half a second before full UTC seconds change. If EVN filename date/time parse failed, start immediately. */
    if (!(NULL == ef->data_start_time_ascii || ef->data_start_time <= 1.0)) {
        u_int64_t timedelta_usec;
        starttime = ef->data_start_time - 0.5;

        assert( gettimeofday(&d, NULL) == 0 );
        timedelta_usec = (u_int64_t)((starttime - (double)d.tv_sec)* 1000000.0) - (double)d.tv_usec;
        fprintf(stderr, "Sleeping until specified time (%s) for %Lu usec...\n", ef->data_start_time_ascii, (ull_t)timedelta_usec);
        usleep_that_works(timedelta_usec);
    }

    /* Check if the client is still connected after the long(?) wait */
    //if(recv(session->client_fd, &status, 1, MSG_PEEK)<0) {
    //    // connection has terminated, exit
    //    fclose(xfer->vsib);
    //    return warn("The client disconnected while server was sleeping.");
    //}

    /* start at next 1PPS pulse */
    start_vsib(session);

    #endif // end of VSIB_REALTIME section

    /* begin round trip time estimation */
    gettimeofday(&ping_s,NULL);

    /* try to signal success to the client */
    status = write(session->client_fd, "\000", 1);
    if (status < 0)
        return warn("Could not signal request approval to client");

    /* read in the block size, target bitrate, and error rate */
    if (read_into(session->client_fd, param->block_size)  < 0) return warn("Could not read block size");
    if (read_into(session->client_fd, param->target_rate) < 0) return warn("Could not read target bitrate");
    if (read_into(session->client_fd, param->error_rate)  < 0) return warn("Could not read error rate");
    net_to_host(param->target_rate);
    net_to_host(param->block_size);
    net_to_host(param->error_rate);

    /* end round trip time estimation */
    gettimeofday(&ping_e,NULL);

    /* read in the slowdown and speedup factors */
    if (read_into(session->client_fd, param->slower_num) < 0) return warn("Could not read slowdown numerator");
    if (read_into(session->client_fd, param->slower_den) < 0) return warn("Could not read slowdown denominator");
    if (read_into(session->client_fd, param->faster_num) < 0) return warn("Could not read speedup numerator");
    if (read_into(session->client_fd, param->faster_den) < 0) return warn("Could not read speedup denominator");
    net_to_host(param->slower_num);
    net_to_host(param->slower_den);
    net_to_host(param->faster_num);
    net_to_host(param->faster_den);

    #ifndef VSIB_REALTIME
    /* try to find the file statistics */
    fseeko64(xfer->file, 0, SEEK_END);
    param->file_size   = ftello64(xfer->file);
    fseeko64(xfer->file, 0, SEEK_SET);
    #else
    /* get length of recording in bytes from filename */
    if (get_aux_entry("flen", ef->auxinfo, ef->nr_auxinfo) != 0) {
        sscanf(get_aux_entry("flen", ef->auxinfo, ef->nr_auxinfo), "%llu", &ull);
        param->file_size = (u_int64_t)ull;
    } else if (get_aux_entry("dl", ef->auxinfo, ef->nr_auxinfo) != 0) {
        sscanf(get_aux_entry("dl", ef->auxinfo, ef->nr_auxinfo), "%llu", &ull);
        param->file_size = (u_int64_t)ull;
    } else {
        param->file_size = 60LL * 512000000LL * 4LL / 8; /* default to amount of bytes equivalent to 4 minutes at 512Mbps */
    }
    fprintf(stderr, "Realtime file length in bytes: %Lu\n", (ull_t)param->file_size);
    #endif

    param->block_count = (param->file_size / param->block_size) + ((param->file_size % param->block_size) != 0);
    param->epoch       = time(NULL);

    /* reply with the length, block size, number of blocks, and run epoch */
    file_size   = param->file_size;    host_to_net(file_size);
    block_size  = param->block_size;   host_to_net(block_size);
    block_count = param->block_count;  host_to_net(block_count);
    epoch       = param->epoch;        host_to_net(epoch);
    if (write_from(session->client_fd, file_size)   < 0) return warn("Could not submit file size");
    if (write_from(session->client_fd, block_size)  < 0) return warn("Could not submit block size");
    if (write_from(session->client_fd, block_count) < 0) return warn("Could not submit block count");
    if (write_from(session->client_fd, epoch)       < 0) return warn("Could not submit run epoch");

    /*calculate and convert RTT to u_sec*/
    session->parameter->wait_u_sec=(ping_e.tv_sec - ping_s.tv_sec)*1000000+(ping_e.tv_usec-ping_s.tv_usec);
    /*add a 10% safety margin*/
    session->parameter->wait_u_sec = session->parameter->wait_u_sec + ((int)(session->parameter->wait_u_sec* 0.1));  

    /* and store the inter-packet delay */
    param->ipd_time   = (u_int32_t) ((1000000LL * 8 * param->block_size) / param->target_rate);
    xfer->ipd_current = param->ipd_time * 3;

    /* if we're doing a transcript */
    if (param->transcript_yn)
        xscript_open(session);

    /* we succeeded! */
    return 0;
}

