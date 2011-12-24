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


#include "mpid_bind.h"
#include "mv_dev.h"
#include "mv_priv.h"
#include "queue.h"
#include "mv_buf.h"
#include "mv_param.h"
#include "mv_inline.h"
#include "mpid_smpi.h"
#include "dreg.h"
#include <math.h>

static inline void mvdev_incoming_eager_start_process(mv_rbuf * v, mvdev_connection_t * c,
        int src_lrank, int tag, int context, int data_len)
{
    MPIR_RHANDLE *rhandle;
    int found;

    MPID_Msg_arrived(src_lrank, tag, context, &rhandle, &found);

    /* This information needs to be filled in whether a preposted 
     * receive was found or not. 
     */
    rhandle->connection = c;
    rhandle->s.count = data_len;
    rhandle->protocol = MVDEV_PROTOCOL_EAGER;
    rhandle->vbufs_received = 1;

    /* recv is already posted */
    if(MVDEV_LIKELY(found)) {
        int truncated = 0; 
        int copy_bytes;

        /* matching packet is here, can no longer cancel recv */
        rhandle->can_cancel = 0;

        D_PRINT("rhandle->len: %d, env: %d\n", rhandle->len, data_len);

        /* what if we get a message longer than what was posted.. */
        if(MVDEV_UNLIKELY(data_len > rhandle->len)) {
            truncated = 1;
        } else {
            rhandle->len = data_len;
        }

        if(MVDEV_LIKELY(0 < (copy_bytes = MIN(rhandle->len, v->byte_len_data)))) {
            memcpy((char *) (rhandle->buf), v->data_ptr, copy_bytes);
        }

        rhandle->bytes_copied_to_user = copy_bytes;

        if(MVDEV_LIKELY(rhandle->len == rhandle->bytes_copied_to_user)) {
            RECV_COMPLETE(rhandle);
            rhandle->s.MPI_ERROR = truncated ?  MPI_ERR_TRUNCATE : MPI_SUCCESS; 
        } else {
            c->rhandle = rhandle;
            rhandle->s.MPI_ERROR = truncated ?  MPI_ERR_TRUNCATE : MPI_SUCCESS;
        }
    } 

    /* Recv not yet posted, set size of incoming data in
     * unexpected queue rhandle
     */
    else {
        mv_buf * buf = MV_Copy_Unexpected_Rbuf(v);

        rhandle->len = data_len;
        rhandle->vbuf_head = rhandle->vbuf_tail = buf;
        rhandle->bytes_copied_to_user = 0;
        c->rhandle = rhandle; 

        D_PRINT("unexpected, srcrank: %d, tag: %d, context: %d \n",
                v->rank, tag, context);
    }

}


static inline void mvdev_incoming_eager_start(mv_rbuf * v, mvdev_connection_t * c,
                                 mvdev_packet_eager_start * header) 
{
    mvdev_incoming_eager_start_process(v, c, header->envelope.src_lrank,
            header->envelope.tag, header->envelope.context, 
            header->envelope.data_length);

    D_PRINT("len: %d, mvparams.use_header_caching: %d\n",
            header->envelope.data_length, mvparams.use_header_caching);


    if(MVDEV_TRANSPORT_RCFP == v->transport && header->envelope.data_length <= 128
            && mvparams.use_header_caching) {
        c->channel_rc_fp->cached_recv.src_lrank = header->envelope.src_lrank;
        c->channel_rc_fp->cached_recv.tag = header->envelope.tag;
        c->channel_rc_fp->cached_recv.context_id = header->envelope.context;
    }
}

void mvdev_recv_cached_eager(mv_rbuf * v, mvdev_connection_t * c)
{
    D_PRINT("into mvdev_recv_cached_eager\n");
    mvdev_incoming_eager_start_process(v, c, 
            c->channel_rc_fp->cached_recv.src_lrank,
            c->channel_rc_fp->cached_recv.tag,
            c->channel_rc_fp->cached_recv.context_id,
            v->byte_len_data);
}

void mvdev_incoming_eager_next(mv_rbuf * v, mvdev_connection_t * c)
{
    MPIR_RHANDLE *rhandle = c->rhandle;
    int copy_bytes;

    if(rhandle->bytes_copied_to_user > 0) {
        copy_bytes = MIN(v->byte_len_data,
                (rhandle->len - rhandle->bytes_copied_to_user));
          
        if(copy_bytes) {
            memcpy((char *) (rhandle->buf) +
                    rhandle->bytes_copied_to_user, v->data_ptr, copy_bytes);
            rhandle->bytes_copied_to_user += copy_bytes;
        }

        if(rhandle->s.count == rhandle->bytes_copied_to_user) {
            int err = rhandle->s.MPI_ERROR;
            RECV_COMPLETE(rhandle);
            rhandle->s.MPI_ERROR = err;
            D_PRINT("completed recv!\n");
        }
    }
    else {
        mv_buf * buf = MV_Copy_Unexpected_Rbuf(v);
        D_PRINT("got a next packet for something that is unexpected\n");

        ((mv_buf *) rhandle->vbuf_tail)->next_ptr = buf;
        rhandle->vbuf_tail = buf;
    }
}

/* By the time we get here we know the message is in order 
 * (at least that is the idea)
 */

void mvdev_process_recv_inorder(mvdev_connection_t * c, mv_rbuf * v) 
{

    D_PRINT("entering mvdev_process_recv_inorder\n");

    if(1 == v->has_header) {
        mvdev_packet_header *p = (mvdev_packet_header *) v->header_ptr;

        switch(p->type) {
            case MVDEV_PACKET_EAGER_START:
                mvdev_incoming_eager_start(v, c, 
                        (mvdev_packet_eager_start *) p);
                break;  
            case MVDEV_PACKET_EAGER_NEXT:
                mvdev_incoming_eager_next(v, c);
                break;  
            case MVDEV_PACKET_RENDEZVOUS_START:
                D_PRINT("Got MVDEV_PACKET_RENDEZVOUS_START\n");
                MV_Rndv_Receive_Start(v, c, 
                        (mvdev_packet_rendezvous_start *) p);
                break;  
            case MVDEV_PACKET_RENDEZVOUS_REPLY:
                D_PRINT("Got MVDEV_PACKET_RENDEZVOUS_REPLY\n");
                MV_Rndv_Receive_Reply(v, c, 
                        (mvdev_packet_rendezvous_reply *) p);
                break;  
            case MVDEV_PACKET_R3_DATA:
                D_PRINT("Got MVDEV_PACKET_R3_DATA\n");
                MV_Rndv_Receive_R3_Data(v, c,
                        (mvdev_packet_r3_data *) p);
                break;  
            case MVDEV_PACKET_UD_ZCOPY_FINISH:
                mvdev_incoming_ud_zcopy_finish(v, c, 
                        (mvdev_packet_ud_zcopy_finish *) p);
                break;
            case MVDEV_PACKET_UD_ZCOPY_ACK:
                mvdev_incoming_ud_zcopy_ack(v, c,
                        (mvdev_packet_ud_zcopy_ack *) p);
                break;
            case MVDEV_PACKET_CH_REQUEST:
                MV_Channel_Incoming_Request(c, (mvdev_packet_conn_pkt *) p);
                break;
            case MVDEV_PACKET_CH_REPLY:
                MV_Channel_Incoming_Reply(c, (mvdev_packet_conn_pkt *) p);
                break;
            case MVDEV_PACKET_CH_ACK:
                MV_Channel_Incoming_Ack(c, (mvdev_packet_conn_pkt *) p);
                break;
            case MVDEV_PACKET_RPUT_FINISH:
                mvdev_incoming_rput_finish(v, c, 
                        (mvdev_packet_rc_rput_finish *) p);
                break;
            case MVDEV_PACKET_RPUT_ACK:
                MV_Rndv_Put_Unreliable_Receive_Ack(c, (mvdev_packet_rput_ack *) p);
                break;
            case MVDEV_PACKET_CH_RCFP_ADDR:
                MV_Recv_RC_FP_Addr(c, (mvdev_packet_rcfp_addr *) p);
                break;
            default:
                error_abort_all(IBV_STATUS_ERR,
                        "Unknown packet type %d in mvdev_process_recv_inorder",
                        p->type);
        }

    } else if(2 == v->has_header) {
        D_PRINT("has_header = 2\n");
        mvdev_recv_cached_eager(v, c);
    } else {
        /* There is no header on this packet. We do know that this is 
         * in order though, so it must go with the last message that came
         * in
         */
        mvdev_protocol_t proto = ((MPIR_RHANDLE *) c->rhandle)->protocol;
        switch(proto) {
            case MVDEV_PROTOCOL_EAGER:
                mvdev_incoming_eager_next(v, c);
                break;
            case MVDEV_PROTOCOL_R3:
                MV_Rndv_Receive_R3_Data_Next(v, c);
                break;
            default:
                error_abort_all(IBV_STATUS_ERR,
                        "Unknown protocol type %d in mvdev_process_recv_inorder",
                        proto);
        }
    }

    release_mv_rbuf(v);
}


void process_flowlist(void)
{
    MPIR_SHANDLE *s;

    while (flowlist) {
        s = flowlist->shandle_head;
        while (s != NULL) {
            MV_Rndv_Push(s);
            if (!s->nearly_complete) {
                break;
            }

            RENDEZVOUS_DONE(flowlist);

            if (s != NULL && (s->protocol == MVDEV_PROTOCOL_R3)) { 
                D_PRINT("Marking as complete\n");
                SEND_COMPLETE(s);
            } 

            s = flowlist->shandle_head;
        }
        /* now move on to the next connection */
        POP_FLOWLIST();
    }

}

