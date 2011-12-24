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
#include "queue.h"

void MV_Rndv_Push(MPIR_SHANDLE * s)
{
    mvdev_connection_t *c = (mvdev_connection_t *) s->connection;

    switch(s->protocol) {
        case MVDEV_PROTOCOL_UD_ZCOPY:
            mvdev_rendezvous_push_zcopy(s, c);
            break;  
        case MVDEV_PROTOCOL_R3:
            MV_Rndv_Push_R3(s, c);
            break;  
        case MVDEV_PROTOCOL_REL_RPUT:
        case MVDEV_PROTOCOL_UNREL_RPUT:
            MV_Rndv_Push_RPut(s, c);
            break;  
        default:
            error_abort_all(IBV_STATUS_ERR,
                    "Invalid protocol %d\n", s->protocol);

    }
}

void MV_Rndv_Receive_Start(mv_rbuf * v, mvdev_connection_t * c,
        mvdev_packet_rendezvous_start * header) 
{
    MPIR_RHANDLE *rhandle;
    int found;
    MPID_Msg_arrived(header->envelope.src_lrank,
            header->envelope.tag,
            header->envelope.context, &rhandle, &found);

    /* This information needs to be filled in whether a preposted 
     * receive was found or not.  */

    rhandle->dreg_entry = NULL; 
    rhandle->connection = c;
    rhandle->send_id = header->sreq;
    rhandle->s.count = header->envelope.data_length;
    rhandle->vbufs_received = 0;
    rhandle->vbuf_head = NULL; 
    rhandle->vbuf_tail = NULL; 
    rhandle->replied = 0;
    rhandle->protocol = header->protocol;
    rhandle->bytes_copied_to_user = 0;

    if (!found) {
        /* recv not yet posted, set size of incoming data in
         * unexpected queue rhandle */
        rhandle->len = header->envelope.data_length;
    } else {
        /* recv posted already, check if recv buffer is large enough */
        if (header->envelope.data_length > rhandle->len) {
            error_abort_all(IBV_STATUS_ERR,
                    "message truncated. ask %d got %d",
                    rhandle->len, header->envelope.data_length);
        }       
        rhandle->len = header->envelope.data_length;

        /* packet now arrived, can no-longer cancel recv */
        rhandle->can_cancel = 0;

        /* can we match sender's protocol? */
        switch (header->protocol) {
            case MVDEV_PROTOCOL_R3:
                mvdev_recv_r3(rhandle);
                break;  
            case MVDEV_PROTOCOL_UD_ZCOPY:
                mvdev_recv_ud_zcopy(rhandle);
                break;  
            case MVDEV_PROTOCOL_UNREL_RPUT:
            case MVDEV_PROTOCOL_REL_RPUT:
                MV_Rndv_Receive_Start_RPut(rhandle);
                D_PRINT("MVDEV_PROTOCOL_RPUT rndv start");
                break;  
            default:
                error_abort_all(IBV_STATUS_ERR, "Invalid protocol %d\n", rhandle->protocol);
        }       
    }
}


void MV_Rndv_Receive_Reply(mv_rbuf * v, mvdev_connection_t * c,
        mvdev_packet_rendezvous_reply * header) 
{
    MPIR_SHANDLE *s = (MPIR_SHANDLE *) (ID_TO_REQ(header->sreq));

    if(s == NULL) { 
        error_abort_all(GEN_ASSERT_ERR, 
                "s == NULL, send handler at mvdev_incoming_rendezvous_reply");
    }

    s->bytes_sent = 0;
    s->receive_id = header->rreq;
    s->nearly_complete = 0;

    switch (s->protocol) {
        case MVDEV_PROTOCOL_R3:
            break;  
        case MVDEV_PROTOCOL_UD_ZCOPY:
            if(header->protocol == MVDEV_PROTOCOL_R3) {
                if(s->dreg_entry != NULL) { 
                    dreg_unregister(s->dreg_entry);
                   s->dreg_entry = NULL; 
                }       
                s->protocol = MVDEV_PROTOCOL_R3;
            } else {
                MV_ASSERT(header->protocol == MVDEV_PROTOCOL_UD_ZCOPY);
                s->remote_qpn = header->rndv_qpn;
                s->seqnum = header->start_seq;
                D_PRINT("got reply: remote_qpn: %u, seq: %u\n",
                        s->remote_qpn, s->seqnum);
                COLLECT_UDZ_INTERNAL_MSG_INFO(s->bytes_total, c);
                s->hca_index = 0;
            }       
            break;  
        case MVDEV_PROTOCOL_REL_RPUT:
        case MVDEV_PROTOCOL_UNREL_RPUT:
            if(header->protocol == MVDEV_PROTOCOL_R3) {
                if(s->dreg_entry != NULL) { 
                    dreg_unregister(s->dreg_entry);
                   s->dreg_entry = NULL; 
                }       
                s->protocol = MVDEV_PROTOCOL_R3;
                s->remote_address = NULL; 
            } else {
                s->remote_address = header->buffer_address;
                s->remote_memhandle_rkey = header->memhandle_rkey;
            }       
            break;  
        default:
            error_abort_all(IBV_STATUS_ERR,
                    "Invalid protocol %d\n", s->protocol);
    }

    RENDEZVOUS_IN_PROGRESS(c, s);
    PUSH_FLOWLIST(c);

    D_PRINT("push onto flow list\n");
}




void MV_Rndv_Send_Reply(MPIR_RHANDLE * rhandle)
{
    mv_sbuf *v;
    mvdev_packet_rendezvous_reply *packet;
    mvdev_connection_t * c = (mvdev_connection_t *) rhandle->connection;

    /* we allow a second reply if the zcopy doesn't work
    if(rhandle->replied) {
        error_abort_all(GEN_EXIT_ERR, "Extra rendezvous reply attempted.");
    }
    */

    v = get_sbuf(c, sizeof(mvdev_packet_rendezvous_reply));
    packet = (mvdev_packet_rendezvous_reply *) v->header_ptr;

    PACKET_SET_HEADER(packet,
                      ((mvdev_connection_t *) rhandle->connection),
                      MVDEV_PACKET_RENDEZVOUS_REPLY);

    MV_ASSERT(packet->header.type == MVDEV_PACKET_RENDEZVOUS_REPLY);

    packet->sreq = rhandle->send_id;
    packet->rreq = REQ_TO_ID(rhandle);
    packet->protocol = rhandle->protocol;

    switch(rhandle->protocol) {
        case MVDEV_PROTOCOL_UD_ZCOPY:
            D_PRINT("Sending rndv_reply : MVDEV_PROTOCOL_UD_ZCOPY\n");
            packet->rndv_qpn = ((mv_qp_pool_entry *) (rhandle->qp_entry))->ud_qp->qp_num;
            packet->start_seq = rhandle->start_seq;
            packet->memhandle_rkey = 0;
            packet->buffer_address = NULL; 
            break;  
        case MVDEV_PROTOCOL_REL_RPUT:
        case MVDEV_PROTOCOL_UNREL_RPUT:
            D_PRINT("Sending rndv_reply : MVDEV_PROTOCOL_RPUT\n");
            packet->memhandle_rkey = (((dreg_entry *) rhandle->dreg_entry)->memhandle[0])->rkey;
            packet->buffer_address = rhandle->buf;
            packet->rndv_qpn = 0;
            packet->start_seq = 0;
            break;  
        case MVDEV_PROTOCOL_R3:
            D_PRINT("Sending rndv_reply : MVDEV_PROTOCOL_R3\n");
            packet->rndv_qpn = 0;
            packet->start_seq = 0;
            packet->memhandle_rkey = 0;
            packet->buffer_address = NULL; 
            break;  
        default:
            error_abort_all(GEN_EXIT_ERR, "Invalid rndv protocol in reply (%d)\n",
                    rhandle->protocol);
    }

    mvdev_post_channel_send(c, v, sizeof(mvdev_packet_rendezvous_reply));

    rhandle->replied = 1;
}

/* TODO: make go over same QP! */
void MV_Rndv_Send_Finish(MPIR_SHANDLE * s) {
    mvdev_connection_t *c = (mvdev_connection_t *) (s->connection);
    mv_sbuf *v = NULL;
    mvdev_packet_rc_rput_finish *packet = NULL;

    switch(s->transport) {
        case MVDEV_TRANSPORT_RC:
            v = get_sbuf_rc(c, sizeof(mvdev_packet_rc_rput_finish)); 
            break;
        case MVDEV_TRANSPORT_XRC:
            v = get_sbuf_xrc(c, sizeof(mvdev_packet_rc_rput_finish)); 
            break;
        default:
            error_abort_all(IBV_STATUS_ERR,
                    "Invalid transport %d\n", s->transport);
            break;
    }

    D_PRINT("sending finish\n");

    packet = (mvdev_packet_rc_rput_finish *) v->header_ptr;

    /* fill in the packet */
    PACKET_SET_HEADER(packet, c, MVDEV_PACKET_RPUT_FINISH);
    packet->rreq = REQ_TO_ID(s->receive_id);

    if(MVDEV_PROTOCOL_UNREL_RPUT == s->protocol) {
        packet->tag = c->tag_counter;

        /* NOW update the counter */
        c->tag_counter = (c->tag_counter + 1) % 1024;
    }

    /* mark MPI send complete when we get an ACK for this message */
    v->shandle = s;

    /* needs to be on the same outgoing qp as the data */
    mvdev_post_channel_send(c, v, sizeof(mvdev_packet_rc_rput_finish));

    D_PRINT("sending MVDEV_PACKET_RPUT_FINISH\n");
}




