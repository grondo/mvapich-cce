/* Copyright (c) 2002-2010, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT_MVAPICH in the top level MPICH directory.
 *
 */

#include "mv_dev.h"
#include "mv_priv.h"
#include "mv_buf.h"
#include "mv_inline.h"
#include "dreg.h"
#include "ibverbs_header.h"
#include <math.h>
#include "req.h"

void MV_Rndv_Push_R3(MPIR_SHANDLE * s, mvdev_connection_t *c) {
    mv_sbuf *v;
    mvdev_packet_r3_data *h;
    int datamax;
    int bytes_in_this_packet;

    /* if we get here, then protocol is R3  */

    if(s->bytes_sent > s->bytes_total) {
        assert(0);
    }

    while (s->nearly_complete == 0 &&
            c->send_window_segments <= mvparams.send_window_segments_max) {

        v = get_sbuf(c, s->bytes_total - s->bytes_sent + sizeof(mvdev_packet_r3_data));
        v->rank = c->global_rank;

        h = (mvdev_packet_r3_data *) v->header_ptr;
        PACKET_SET_HEADER(h, c, MVDEV_PACKET_R3_DATA);
        h->rreq = s->receive_id;

        /* figure out how much data to send in this packet */

        datamax = SBUF_MAX_DATA_SZ_HDR(v, mvdev_packet_r3_data);

        bytes_in_this_packet = s->bytes_total - s->bytes_sent;
        if (datamax < bytes_in_this_packet) {
            bytes_in_this_packet = datamax;
        }

        /* sometimes we send a zero byte R3 message since we
         * need to reply to a rndv reply
         */
        if(bytes_in_this_packet) {
            memcpy(((char *) h) + sizeof(mvdev_packet_r3_data), ((char *) s->local_address) +
                    s->bytes_sent, bytes_in_this_packet);
        }

        mvdev_post_channel_send(c, v, bytes_in_this_packet + sizeof(mvdev_packet_r3_data));

        s->bytes_sent += bytes_in_this_packet;
        if (s->bytes_sent == s->bytes_total) {
            v->shandle = s;
            s->nearly_complete = 1;
        }
        if(s->bytes_sent > s->bytes_total) {
            fprintf(stderr, "sent too much!\n");
        }
    }

    if (s->nearly_complete) {
        if(s->blocked) {
            c->blocked_count--;
            s->blocked = 0;
        }
    } else {
        if(!s->blocked) {
            c->blocked_count++;
        } else {
            s->blocked = 1;
        }       
    }
}

void MV_Rndv_Receive_R3_Data(mv_rbuf * v, mvdev_connection_t * c,
        mvdev_packet_r3_data * h)
{
    MPIR_RHANDLE *rhandle = (MPIR_RHANDLE *) ID_TO_REQ(h->rreq);

    memcpy(((char *) rhandle->buf) +
           rhandle->bytes_copied_to_user, v->data_ptr, v->byte_len_data);
    rhandle->bytes_copied_to_user += v->byte_len_data;

    if(rhandle->bytes_copied_to_user > rhandle->len) {
        fprintf(stderr, "copied: %d, len: %d, this: %d\n", 
                rhandle->bytes_copied_to_user, rhandle->len, v->byte_len_data);
    }   
    MV_ASSERT(rhandle->bytes_copied_to_user <= rhandle->len);

    if (rhandle->bytes_copied_to_user == rhandle->len) {
        RECV_COMPLETE(rhandle);
        D_PRINT("VI %3d R3 RECV COMPLETE total %d\n",
                c->global_rank, rhandle->len);
    } else {
        c->rhandle = rhandle;
    }   
}

void MV_Rndv_Receive_R3_Data_Next(mv_rbuf * v, mvdev_connection_t * c)
{
    MPIR_RHANDLE *rhandle = (MPIR_RHANDLE *) c->rhandle;;

    memcpy(((char *) rhandle->buf) + rhandle->bytes_copied_to_user, 
            v->data_ptr, v->byte_len_data);

    rhandle->bytes_copied_to_user += v->byte_len_data;

    if(rhandle->bytes_copied_to_user > rhandle->len) {
        fprintf(stderr, "copied: %d, len: %d, this: %d\n",
                rhandle->bytes_copied_to_user, rhandle->len, v->byte_len_data);
    }
    MV_ASSERT(rhandle->bytes_copied_to_user <= rhandle->len);

    if (rhandle->bytes_copied_to_user == rhandle->len) {
        RECV_COMPLETE(rhandle);
        D_PRINT("R3 recv complete from rank %d total %d",
                c->global_rank, rhandle->len);
    }
}


