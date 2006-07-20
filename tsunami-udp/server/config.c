/*========================================================================
 * config.h  --  Tuneable parameters for Tsunami server.
 *
 * This contains tuneable parameters for the Tsunami file transfer server.
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

#include <string.h>  /* for memset() */

#include "server.h"


/*------------------------------------------------------------------------
 * Global constants.
 *------------------------------------------------------------------------*/

const u_int32_t  DEFAULT_BLOCK_SIZE    = 32768;     /* default size of a single file block     */
const u_char    *DEFAULT_SECRET        = "kitten";  /* default shared secret                   */
const u_int16_t  DEFAULT_TCP_PORT      = 46224;     /* default TCP port to listen on           */
const u_int32_t  DEFAULT_UDP_BUFFER    = 20000000;  /* default size of the UDP transmit buffer */
const u_char     DEFAULT_VERBOSE_YN    = 1;         /* the default verbosity setting           */
const u_char     DEFAULT_TRANSCRIPT_YN = 0;         /* the default transcript setting          */
const u_char     DEFAULT_IPV6_YN       = 0;         /* the default IPv6 setting                */
const u_char     DEFAULT_NO_RETRANSMIT = 0;         /* server-side setting, on default use retransmission */

/*------------------------------------------------------------------------
 * void reset_server(ttp_parameter_t *parameter);
 *
 * Resets the set of default parameters to their default values.
 *------------------------------------------------------------------------*/
void reset_server(ttp_parameter_t *parameter)
{
    /* zero out the memory structure */
    memset(parameter, 0, sizeof(*parameter));

    /* fill the fields with their defaults */
    parameter->block_size    = DEFAULT_BLOCK_SIZE;
    parameter->secret        = DEFAULT_SECRET;
    parameter->tcp_port      = DEFAULT_TCP_PORT;
    parameter->udp_buffer    = DEFAULT_UDP_BUFFER;
    parameter->verbose_yn    = DEFAULT_VERBOSE_YN;
    parameter->transcript_yn = DEFAULT_TRANSCRIPT_YN;
    parameter->ipv6_yn       = DEFAULT_IPV6_YN;
    parameter->no_retransmit = DEFAULT_NO_RETRANSMIT;
}


/*========================================================================
 * $Log$
 * Revision 1.1  2006/07/10 12:39:52  jwagnerhki
 * added to trunk
 *
 */