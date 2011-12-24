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

void MV_Rndv_Receive_Start_RPut(MPIR_RHANDLE * rhandle)
{
    /* only way buffer is NULL is if length is zero */
    if(NULL == rhandle->buf) {
        rhandle->buf = &nullrbuffer;
    }   

    /* try to register the buffer directly */
    rhandle->dreg_entry = dreg_register(rhandle->buf,
            rhandle->len, DREG_ACL_WRITE);
    if(NULL == rhandle->dreg_entry) {
        /* failed to register memory, revert to R3 */
        D_PRINT("Cannot register mem -- using R3\n");
        rhandle->protocol = MVDEV_PROTOCOL_R3;
        mvdev_recv_r3(rhandle);
        return; 
    }   

    rhandle->qp_entry = NULL;
    D_PRINT("registered memory for recving rput\n");

    MV_Rndv_Send_Reply(rhandle);
}

void MV_Rndv_Push_RPut(MPIR_SHANDLE * s, mvdev_connection_t *c) 
{
    int nbytes;
    mv_sbuf * v;

    if(s->bytes_sent) { 
        error_abort_all(GEN_ASSERT_ERR, "s->bytes_sent != 0 Rendezvous Push");
    }    

    if(s->bytes_total > 0) { 
        MV_ASSERT(s->dreg_entry != NULL);
        MV_ASSERT(s->remote_address != NULL);
    }    

    while(s->bytes_sent < s->bytes_total) {

        switch(s->transport) {
            case MVDEV_TRANSPORT_RC:
                v = get_sbuf_rc(c, 0);
                break;
            case MVDEV_TRANSPORT_XRC:
                v = get_sbuf_xrc(c, 0);
                break;
            default:
                error_abort_all(GEN_ASSERT_ERR, "invalid transport");
        }

        nbytes = s->bytes_total - s->bytes_sent;

        if (nbytes > (int) mvparams.maxtransfersize) {
            nbytes = mvparams.maxtransfersize;
        }    

        MV_Rndv_Put(c, v, s,
                (char *) (s->local_address) + s->bytes_sent,
                ((dreg_entry *) s->dreg_entry)->memhandle[0]->lkey,
                (char *) (s->remote_address) + s->bytes_sent,
                s->remote_memhandle_rkey, nbytes);
        s->bytes_sent += nbytes;
    }
    MV_ASSERT(s->bytes_sent == s->bytes_total);

    MV_Rndv_Send_Finish(s);
    s->nearly_complete = 1;
}

void MV_Rndv_Put(mvdev_connection_t * c, mv_sbuf * v, MPIR_SHANDLE * s,
        void *local_address,
        uint32_t local_memhandle, void *remote_address,
        uint32_t remote_memhandle, int nbytes) 
{
    uint32_t srqn = 0;
    mv_qp * qp = NULL;
    v->rel_type = MV_RELIABLE_NONE;
    v->segments = 1;

    COLLECT_RCR_INTERNAL_MSG_INFO(nbytes, c);

    switch(s->transport) {
        case MVDEV_TRANSPORT_RC:
            {
                mvdev_channel_rc * h = c->rc_channel_head;
                qp = &(h->qp[0]);
                break;
            }
#ifdef XRC
        case MVDEV_TRANSPORT_XRC:
            {
                qp = &(c->xrc_channel.shared_channel->qp[0]);
                srqn = c->xrc_channel.remote_srqn_head->srqn[0];
                break;
            }
#endif
    }

    switch(s->protocol) {
        case MVDEV_PROTOCOL_UNREL_RPUT:
            MV_Rndv_Put_Unreliable_Init(c, v, local_address, local_memhandle,
                    remote_address, remote_memhandle, nbytes, qp, srqn);

            /* if this is going over a flow controlled channel we need to
             * make sure it takes that path since we are using an immediate
             * (which consumes a receive)
             */
            if(MVDEV_CH_RC_RQ == qp->type) {
                v->seqnum = v->seqnum_last = c->seqnum_next_tosend;
                v->rel_type = MV_RELIABLE_FREE_SBUF;
                v->flag = MVDEV_RPUT_FLAG;

                if(MVDEV_UNLIKELY(qp->send_credits_remaining < v->segments) || (qp->ext_backlogq_head)) {
                    D_PRINT("Backlog\n");
                    mvdev_ext_backlogq_queue(qp, &(v->desc));
                } else {
                    --(qp->send_credits_remaining);
                    SEND_RC_SR(qp, v);
                    D_PRINT("sent (%d)\n", qp->send_credits_remaining);
                }
                /* get free'd when the finish is acked */
                mvdev_track_send(c, v);
            } else {
                SEND_RC_SR(qp, v);
            }
            break;  
        case MVDEV_PROTOCOL_REL_RPUT:
            MV_Rndv_Put_Reliable_Init(v, local_address, local_memhandle,
                    remote_address, remote_memhandle, nbytes, qp, srqn);
            SEND_RC_SR(qp, v);
            break;  
    }
}

void mvdev_incoming_rput_finish(mv_rbuf * v, mvdev_connection_t * c,
        mvdev_packet_rc_rput_finish * h)
{
    MPIR_RHANDLE *rhandle = (MPIR_RHANDLE *) ID_TO_REQ(h->rreq);

    switch(rhandle->protocol) {
        case MVDEV_PROTOCOL_UNREL_RPUT:
            /* in this case we need to check to verify that we have received
             * immediate information (and successful transmission) and
             * send back an acknowledgement
             */
            MV_Rndv_Put_Unreliable_Receive_Finish(v, c, h, rhandle);
            break;  
        case MVDEV_PROTOCOL_REL_RPUT:
            rhandle->bytes_copied_to_user = rhandle->len;
            RECV_COMPLETE(rhandle);
            break;  
        default:
            assert(0);
    }


}



