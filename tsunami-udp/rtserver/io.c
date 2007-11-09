/*========================================================================
 * io.c  --  Disk I/O routines for Tsunami server.
 *
 * This contains disk I/O routines for the Tsunami file transfer server.
 *
 * Written by Mark Meiss (mmeiss@indiana.edu).
 * Copyright © 2002 The Trustees of Indiana University.
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
 * NO WARRANTIES AS TO CAPABILITIES OR ACCURACY ARE MADE. INDIANA
 * UNIVERSITY GIVES NO WARRANTIES AND MAKES NO REPRESENTATION THAT
 * SOFTWARE IS FREE OF INFRINGEMENT OF THIRD PARTY PATENT, COPYRIGHT,
 * OR OTHER PROPRIETARY RIGHTS.  INDIANA UNIVERSITY MAKES NO
 * WARRANTIES THAT SOFTWARE IS FREE FROM "BUGS", "VIRUSES", "TROJAN
 * HORSES", "TRAP DOORS", "WORMS", OR OTHER HARMFUL CODE.  LICENSEE
 * ASSUMES THE ENTIRE RISK AS TO THE PERFORMANCE OF SOFTWARE AND/OR
 * ASSOCIATED MATERIALS, AND TO THE PERFORMANCE AND VALIDITY OF
 * INFORMATION GENERATED USING SOFTWARE.
 *========================================================================*/

#include <tsunami-server.h>
// #define MODE_34TH 1 /* uncomment for 3/4th rate mode, valid in VSIB_REALTIME build */

/*------------------------------------------------------------------------
 * int build_datagram(ttp_session_t *session, u_int64_t block_index,
 *                    u_int16_t block_type, u_char *datagram);
 *
 * Constructs to hold the given block of data, with the given type
 * stored in it.  The format of the datagram is:
 *
 *     64                    0
 *     +---------------------+
 *     |     block_number    |
 *     +----------+----------+
 *     |   type   |   data   |
 *     +----------+     :    |
 *     :     :          :    :
 *     +---------------------+
 *
 * The datagram is stored in the given buffer, which must be at least
 * ten bytes longer than the block size for the transfer.  Returns 0 on
 * success and non-zero on failure.
 *
 * There are two versions of this function. One is for the file server
 * that reads data from normal files. The other is for the realtime server
 * that reads data from the VSIB ring buffer and can create a local
 * backup file of all the data.
 *
 *------------------------------------------------------------------------*/
#ifndef VSIB_REALTIME
int build_datagram(ttp_session_t *session, u_int64_t block_index,
                   u_int16_t block_type, u_char *datagram)
{
    static u_int32_t last_block = 0;
    int              status;
    blockheader_t   *hdr     = (blockheader_t*)datagram;
    u_char          *payload = datagram + sizeof(blockheader_t);

    /* assemble the datagram header */
    hdr->block = block_index; host_to_net(hdr->block);
    hdr->type  = block_type;  host_to_net(hdr->type);

    #ifdef DISKLESS
    /* do not read data if debugging or throughput test*/
    return 0;
    #else

    /* move the file pointer to the appropriate location */
    // if (block_index != (last_block + 1)) {
        fseeko64( session->transfer.file,
                  ((u_int64_t) session->parameter->block_size) * (block_index - 1),
                  SEEK_SET );
    // }

    /* read payload data into the datagram */
    status = fread( payload,
                    1, session->parameter->block_size,
                    session->transfer.file );
    if (status < 0) {
        sprintf(g_error, "Could not read block #%llu", block_index);
        return warn(g_error);
    }


    /* return success */
    last_block = block_index;
    return 0;
    #endif
}

#else /* VSIB_REALTIME */

int build_datagram(ttp_session_t *session, u_int64_t block_index,
		   u_int16_t block_type, u_char *datagram)
{
    blockheader_t   *hdr          = (blockheader_t*)datagram;
    u_char          *payload       = datagram + sizeof(blockheader_t);
    u_int32_t        block_size    = session->parameter->block_size;
    u_int64_t        block_size64  = (u_int64_t)block_size;
    static u_int64_t last_block    = 0;
    static u_int64_t last_written_vsib_block = 0;
    size_t           status        = 0;
    size_t           write_size    = 0;
    #ifdef MODE_34TH
    static u_char    packingbuffer[4*MAX_BLOCK_SIZE/3+8];
    static u_int64_t vsib_byte_pos = 0L;
    u_int32_t        inbufpos      = 0;
    u_int32_t        outbufpos     = 0;
    #endif

    /* assemble the datagram header */
    hdr->block = block_index; host_to_net(hdr->block);
    hdr->type  = block_type;  host_to_net(hdr->type);

    /* reset static vars to zero at first block, so that the next transfer works ok also */
    if (1 == block_index) {
        last_written_vsib_block = 0;
        last_block = 0;
    }

    #ifdef MODE_34TH
    /* ----------------------------------------------------------------------
     * Take 3 bytes skip 4th : 6 lower BBCs included, 2 upper discarded
     * ---------------------------------------------------------------------- */

    /* calculate sent bytes plus the offset caused by discarded channel bytes */
    vsib_byte_pos = (block_size64)*(block_index - 1) + (block_size64)*(block_index - 1)/3; /* 4/3 */

    /* read enough data to get over 4/3th of blocksize */
    fseeko64(session->transfer.vsib, vsib_byte_pos, SEEK_SET);
    read_vsib_block(packingbuffer,  2 * block_size + 4); /* 2* vs. 4/3* */

    /* copy, pack */
    inbufpos=0; outbufpos=0;
    while (outbufpos < block_size) {
        if (3 == (vsib_byte_pos & 3)) {
            inbufpos++; vsib_byte_pos++;
        } else {
            *(payload + (outbufpos++)) = packingbuffer[inbufpos++];
            vsib_byte_pos++;
        }
    }

    #else

    /* ----------------------------------------------------------------------
     * Normal read mode : take all data
     * ---------------------------------------------------------------------- */

    /* seek to non-sequential location if needed */
    if (block_index != (last_block + 1)) {
        fseeko64(session->transfer.vsib, block_size64 * (block_index - 1), SEEK_SET);
    }

    /* read payload data into the datagram */
    read_vsib_block(payload, block_size);

    #endif

    /* need to maintain local backup copy of sent data? */
    if (  (session->parameter->fileout) && (block_index != 0)
          && (block_index == (last_written_vsib_block + 1)) )
    {
        /* remember what we have stored */
        last_written_vsib_block++;

        /* figure out how many bytes to write */
        write_size = (block_index == session->parameter->block_count) ?
                        (session->parameter->file_size % block_size)  :
                        block_size;
        if (write_size == 0)
            write_size = block_size;

        /* write the block to disk */
        status = fwrite(payload, 1, write_size, session->transfer.file);
        if (status < write_size) {
           sprintf(g_error, "Could not write block %llu of file", block_index);
           return warn(g_error);
        }
    }

    /* return success */
    return 0;
}
#endif

/*========================================================================
 * $Log$
 *
 */
