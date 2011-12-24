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

void MV_Rndv_Put_Unreliable_Init(mvdev_connection_t *c, mv_sbuf * v, void *local_address,
        uint32_t lkey, void *remote_address, uint32_t rkey, int len, mv_qp * qp, uint32_t srqn)
{
    uint32_t temp_rank = mvdev.me, temp_counter = c->tag_counter;

#ifdef XRC
    if(MVDEV_TRANSPORT_XRC == v->transport) {
        v->desc.sr.xrc_remote_srq_num = srqn; 
    }
#endif


    v->segments = 1;

    v->desc.sr.next = NULL;
    v->desc.sr.send_flags = IBV_SEND_SIGNALED;
    v->desc.sr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
    v->desc.sr.wr_id = (aint_t) (&(v->desc));
    v->desc.sr.imm_data = CREATE_PUT_IMM(temp_rank, temp_counter);

    v->desc.sr.num_sge = 1;
    v->desc.sr.sg_list = &(v->desc.sg_entry);

    v->desc.sg_entry.length = len;
    v->desc.sg_entry.lkey = lkey;
    v->desc.sg_entry.addr = (uintptr_t) local_address;

    v->desc.sr.wr.rdma.remote_addr = (uintptr_t) remote_address;
    v->desc.sr.wr.rdma.rkey = rkey;

    v->desc.qp = qp;
}


void MV_Rndv_Put_Unreliable_Receive_Finish(mv_rbuf * v, mvdev_connection_t * c,
        mvdev_packet_rc_rput_finish * h, MPIR_RHANDLE * rhandle) {
    int found = 0;
    mv_rndv_rput_unrel_tag * current = c->unrel_tags_head;

    /* check the queue for the 32bit tag to see if we have gotten this 
     * one already (hopefully we have)
     */
    while(NULL != current) {
        if(current->tag == h->tag) {
            MV_Rndv_Put_Unreliable_Send_Ack(c, rhandle);
            rhandle->bytes_copied_to_user = rhandle->len;
            RECV_COMPLETE(rhandle);
            found = 1;

            if(current == c->unrel_tags_head) {
                c->unrel_tags_head = c->unrel_tags_head->next;
                if(NULL != c->unrel_tags_head) {
                    c->unrel_tags_head->prev = NULL;
                }
            } else {
                if(NULL != current->next) {
                    current->next->prev = current->prev;
                    current->prev->next = current->next;
                } else {
                    current->prev->next = NULL;
                }
            }

            free(current);
            break;
        }
    }

    /* otherwise request resend of the information */
    if(!found) {
        fprintf(stderr, "NOT FOUND!\n");
    }
}

void MV_Rndv_Put_Unreliable_Receive_Ack(mvdev_connection_t * c, mvdev_packet_rput_ack * h) {
    MPIR_SHANDLE *s = (MPIR_SHANDLE *) (ID_TO_REQ(h->sreq));

    if(s == NULL) { 
        error_abort_all(GEN_ASSERT_ERR, 
                "s == NULL, send handler at mvdev_incoming_rendezvous_reply");
    }   

    MV_ASSERT(MVDEV_PROTOCOL_UNREL_RPUT == s->protocol);
    SEND_COMPLETE(s);
}

void MV_Rndv_Put_Unreliable_Send_Ack(mvdev_connection_t * c, 
        MPIR_RHANDLE * rhandle) {
    mv_sbuf *v = NULL;
    mvdev_packet_rput_ack *packet = NULL;

    v = get_sbuf(c, sizeof(mvdev_packet_rput_ack)); 

    packet = (mvdev_packet_rput_ack *) v->header_ptr;

    /* fill in the packet */
    PACKET_SET_HEADER(packet, c, MVDEV_PACKET_RPUT_ACK);
    packet->sreq = rhandle->send_id;

#ifdef MV_PROFILE
    c->msg_info.control_ack++;
#endif

    /* needs to be on the same outgoing qp as the data */
    mvdev_post_channel_send(c, v, sizeof(mvdev_packet_rput_ack));
}

void MV_Rndv_Put_Unreliable_Receive_Imm(mv_rbuf * v, uint32_t imm_data) {
    mv_rndv_rput_unrel_tag * current;
    mvdev_connection_t * c;
    int found = 0;
    uint32_t rank = 0, tag = 0;

    READ_PUT_IMM(imm_data, &rank, &tag);
    c = &(mvdev.connections[rank]);
    current = c->unrel_tags_head;

    while(NULL != current) {
        if(current->tag == tag) {
            found = 1;
            break;
        }
    }

    if(!found) {
        current = (mv_rndv_rput_unrel_tag *) malloc(sizeof(mv_rndv_rput_unrel_tag));
        current->tag = tag;
        current->next = c->unrel_tags_head;
        current->prev = NULL;
        if(c->unrel_tags_head != NULL) {
            c->unrel_tags_head->prev = current;
        }
        c->unrel_tags_head = current;
    }

    ACK_CREDIT_CHECK(c, v);
}

/* we'll need to have a function here to do:
 *  - Send ACK
 *  - Recv ACK
 *      - check completions
 *  - handle imm data completions
 *  - error recovery

 */
