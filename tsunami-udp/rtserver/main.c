/*========================================================================
 * main.c  --  Main routine and supporting function for Tsunami server.
 *
 * This is the persistent process that sends out files upon request.
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

#define _GNU_SOURCE

#include <errno.h>       /* for the errno variable and perror()   */
#include <fcntl.h>       /* for the fcntl() function              */
#include <getopt.h>      /* for getopt_long()                     */
#include <signal.h>      /* for standard POSIX signal handling    */
#include <stdlib.h>      /* for memory allocation, exit(), etc.   */
#include <string.h>      /* for memset(), sprintf(), etc.         */
#include <sys/types.h>   /* for standard system data types        */
#include <sys/socket.h>  /* for the BSD sockets library           */
#include <sys/wait.h>    /* for waitpid()                         */
#include <unistd.h>      /* for Unix system calls                 */

#include "server.h"


/*------------------------------------------------------------------------
 * Function prototypes (module scope).
 *------------------------------------------------------------------------*/

void client_handler (ttp_session_t *session);
void process_options(int argc, char *argv[], ttp_parameter_t *parameter);
void reap           (int signum);


/*------------------------------------------------------------------------
 * MAIN PROGRAM
 *------------------------------------------------------------------------*/
int main(int argc, char *argv[])
{
    int                server_fd, client_fd;
    struct sockaddr_in remote_address;
    int                remote_length = sizeof(struct sockaddr_in);
    ttp_parameter_t    parameter;
    ttp_session_t      session;
    pid_t              child_pid;

    /* initialize our parameters */
    reset_server(&parameter);

    /* process our command-line options */
    process_options(argc, argv, &parameter);

    /* obtain our server socket */
    server_fd = create_tcp_socket(&parameter);
    if (server_fd < 0) {
	sprintf(g_error, "Could not create server socket on port %d", parameter.tcp_port);
	return error(g_error);
    }

    /* install a signal handler for our children */
    signal(SIGCHLD, reap);

    /* while our little world keeps turning */
    while (1) {

	/* accept a new client connection */
	client_fd = accept(server_fd, (struct sockaddr *) &remote_address, &remote_length);
	if (client_fd < 0) {
	    warn("Could not accept client connection");
	    continue;
	}

	/* and fork a new child process to handle it */
	child_pid = fork();
	if (child_pid < 0) {
	    warn("Could not create child process");
	    continue;
	}

	/* if we're the child */
	if (child_pid == 0) {

	    /* close the server socket */
	    close(server_fd);

	    /* set up the session structure */
	    session.client_fd = client_fd;
	    session.parameter = &parameter;
	    memset(&session.transfer, 0, sizeof(session.transfer));

	    /* and run the client handler */
	    client_handler(&session);
	    return 0;

	/* if we're the parent */
	} else

	    /* close the client socket */
	    close(client_fd);
    }
}


/*------------------------------------------------------------------------
 * void client_handler(ttp_session_t *session);
 *
 * This routine is run by the client processes that are created in
 * response to incoming connections.
 *------------------------------------------------------------------------*/
void client_handler(ttp_session_t *session)
{
    retransmission_t  retransmission;                /* the retransmission data object                 */
    struct timeval    start, stop;                   /* the start and stop times for the transfer      */
    struct timeval    delay;                         /* the interpacket delay value                    */
    int               result;                        /* number of bytes read from retransmission queue */
    u_char            datagram[MAX_BLOCK_SIZE + 6];  /* the datagram containing the file block         */
    u_int64_t         ipd_time;                      /* the time to delay after this packet            */
    int               status;
    ttp_transfer_t   *xfer  = &session->transfer;
    ttp_parameter_t  *param =  session->parameter;
    u_int64_t         delta;

    /* negotiate the connection parameters */
    status = ttp_negotiate(session);
    if (status < 0)
	error("Protocol revision number mismatch");

    /* have the client try to authenticate to us */
    status = ttp_authenticate(session, session->parameter->secret);
    if (status < 0)
	error("Client authentication failure");

    /* while we haven't been told to stop */
    while (1) {

	/* make the client descriptor blocking */
	status = fcntl(session->client_fd, F_SETFL, 0);
	if (status < 0)
	    error("Could not make client socket blocking");

	/* negotiate another transfer */
	status = ttp_open_transfer(session);
	if (status < 0) {
	    warn("Invalid file request");
	    continue;
	}

	/* negotiate a data transfer port */
	status = ttp_open_port(session);
	if (status < 0) {
	    warn("UDP socket creation failed");
	    continue;
	}

	/* make the client descriptor non-blocking again */
	status = fcntl(session->client_fd, F_SETFL, O_NONBLOCK);
	if (status < 0)
	    error("Could not make client socket non-blocking");

	/*---------------------------
	 * START TIMING
	 *---------------------------*/
	gettimeofday(&start, NULL);
	if (param->transcript_yn)
	    xscript_data_start(session, &start);

	/* start by blasting out every block */
	xfer->block = 0;
	while (xfer->block <= param->block_count) {

	    /* see if transmit requests are available */
	    gettimeofday(&delay, NULL);
	    result = read(session->client_fd, &retransmission, sizeof(retransmission));
	    if ((result <= 0) && (errno != EAGAIN))
		error("Retransmission read failed");

	    /* if we have a retransmission */
	    if (result == sizeof(retransmission_t)) {

		/* if it's a stop request, go back to waiting for a filename */
		if (ntohs(retransmission.request_type) == REQUEST_STOP) {
		    fprintf(stderr, "Transmission complete.\n");
		    break;
		}

		/* otherwise, handle the retransmission */
		status = ttp_accept_retransmit(session, &retransmission, datagram);
		if (status < 0)
		    warn("Retransmission error");
		usleep_that_works(75);

	    /* if we have no retransmission */
	    } else if (result <= 0) {

		/* build the block */
		xfer->block = min(xfer->block + 1, param->block_count);
		result = build_datagram(session, xfer->block, (xfer->block == param->block_count) ? 'X' : 'O', datagram);
		if (result < 0) {
		    sprintf(g_error, "Could not read block #%u", xfer->block);
		    error(g_error);
		}

		/* transmit the block */
		result = sendto(xfer->udp_fd, datagram, 6 + param->block_size, 0, xfer->udp_address, xfer->udp_length);
		if (result < 0) {
		    sprintf(g_error, "Could not transmit block #%u", xfer->block);
		    warn(g_error);
		    continue;
		}

	    /* if we have a partial retransmission message */
	    } else if (result > 0) {

		/* loop until we clear out the broken message */
		int sofar = result;
		while (sofar < sizeof(retransmission)) {
		    result = read(session->client_fd, &retransmission, sizeof(retransmission) - sofar);
		    if ((result < 0) && (errno != EAGAIN))
			error("Split message recovery failed");
		    else if (result > 0)
			sofar += result;
		}
		continue;
	    }

	    /* delay for the next packet */
	    ipd_time = get_usec_since(&delay);
	    ipd_time = ((ipd_time + 50) < xfer->ipd_current) ? ((u_int64_t) (xfer->ipd_current - ipd_time - 50)) : 0;
	    usleep_that_works(ipd_time);
	}

	/*---------------------------
	 * STOP TIMING
	 *---------------------------*/
	gettimeofday(&stop, NULL);
	if (param->transcript_yn)
	    xscript_data_stop(session, &stop);
	delta = 1000000LL * (stop.tv_sec - start.tv_sec) + stop.tv_usec - start.tv_usec;

	/* report on the transfer */
	if (param->verbose_yn)
	    fprintf(stderr, "Transferred %llu bytes in %0.2f seconds (%0.1f Mbps)\n", param->file_size, delta / 1000000.0, 8.0 * param->file_size / delta);

	/* close the transcript */
	if (param->transcript_yn)
	    xscript_close(session, delta);

	/* close the file and the UDP socket */
        if (param->fileout)         /* only if file output was requested */
         	fclose(xfer->file);
	close(xfer->udp_fd);
	memset(xfer, 0, sizeof(*xfer));

        /* stop the VSIB */
	stop_vsib(session);

	/* Free the ring buffer */
	free(param->ringbuf);
    }
}


/*------------------------------------------------------------------------
 * void process_options(int argc, char *argv[],
 *                      ttp_parameter_t *parameter);
 *
 * Processes the command-line options and sets the protocol parameters
 * as appropriate.
 *------------------------------------------------------------------------*/
void process_options(int argc, char *argv[], ttp_parameter_t *parameter)
{
    struct option long_options[] = { { "verbose",    0, NULL, 1 },
				     { "transcript", 0, NULL, 2 },
				     { "v6",         0, NULL, 3 },
				     { "port",       1, NULL, 4 },
				     { "secret",     1, NULL, 5 },
				     { "datagram",   1, NULL, 6 },
				     { "buffer",     1, NULL, 7 },
				     { NULL,         0, NULL, 0 } };
    int           which;

    /* for each option found */
    while ((which = getopt_long(argc, argv, "+", long_options, NULL)) > 0) {

	/* depending on which option we got */
	switch (which) {

	    /* --verbose    : enter verbose mode for debugging */
	    case 1:  parameter->verbose_yn = 1;
		     break;

	    /* --transcript : enter transcript mode for recording stats */
	    case 2:  parameter->transcript_yn = 1;
	             break;

	    /* --v6         : enter IPv6 mode */
	    case 3:  parameter->ipv6_yn = 1;
	             break;

	    /* --port=i     : port number for the server */
	    case 4:  parameter->tcp_port   = atoi(optarg);
		     break;

	    /* --secret=s   : shared secret for the client and server */
	    case 5:  parameter->secret     = optarg;
		     break;

	    /* --datagram=i : size of datagrams in bytes */
	    case 6:  parameter->block_size = atoi(optarg);
		     break;

	    /* --buffer=i   : size of socket buffer */
	    case 7:  parameter->udp_buffer = atoi(optarg);
		     break;

	    /* otherwise    : display usage information */
	    default: fprintf(stderr, "Usage: tsunamid [--verbose] [--transcript] [--v6] [--port=n] [--datagram=bytes] [--buffer=bytes]\n\n");
		     fprintf(stderr, "Defaults: verbose    = %d\n",   DEFAULT_VERBOSE_YN);
		     fprintf(stderr, "          transcript = %d\n",   DEFAULT_TRANSCRIPT_YN);
		     fprintf(stderr, "          v6         = %d\n",   DEFAULT_IPV6_YN);
		     fprintf(stderr, "          port       = %d\n",   DEFAULT_TCP_PORT);
		     fprintf(stderr, "          datagram   = %d\n",   DEFAULT_BLOCK_SIZE);
		     fprintf(stderr, "          buffer     = %d\n\n", DEFAULT_UDP_BUFFER);
		     fprintf(stderr, "verbose    : turns on verbose output mode\n");
		     fprintf(stderr, "transcript : turns on transcript mode for statistics recording\n");
		     fprintf(stderr, "v6         : operates using IPv6 instead of (not in addition to!) IPv4\n");
		     fprintf(stderr, "port       : specifies which TCP port on which to listen to incoming connections\n");
		     fprintf(stderr, "secret     : specifies the shared secret for the client and server\n");
		     fprintf(stderr, "datagram   : specifies the desired datagram size (in bytes)\n");
		     fprintf(stderr, "buffer     : specifies the desired size for UDP socket send buffer (in bytes)\n");
		     exit(1);
	}
    }
}


/*------------------------------------------------------------------------
 * void reap(int signum);
 *
 * A signal handler to take care of our children's deaths (SIGCHLD).
 *------------------------------------------------------------------------*/
void reap(int signum)
{
    int status;

    /* accept as many deaths as we can */
    while (waitpid(-1, &status, WNOHANG) > 0);

    /* reenable the handler */
    signal(SIGCHLD, reap);
}


/*========================================================================
 * $Log$
 * Revision 1.1  2006/07/10 12:37:21  jwagnerhki
 * added to trunk
 *
 */
