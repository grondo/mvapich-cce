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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "mv_param.h"
#include "mv_priv.h"
#include "mv_buf.h"
#include "mv_inline.h"


void MV_Channel_Activate(mvdev_connection_t * c, 
        mvdev_channel_request * req, mvdev_packet_conn_pkt * pkt) 
{
    MV_ASSERT(req->rank == c->global_rank);

    switch(req->req_type) {
        case MVDEV_CH_RC_SRQ:
            {
                mvdev_channel_request_srq * srq_req = 
                    (mvdev_channel_request_srq *) req;
                c->rc_enabled = 1;
                c->alloc_srq = 0;
 
                if(NULL == c->rc_channel_head) {
                    mvdev_channel_rc * new = (mvdev_channel_rc *) srq_req->channel_ptr;
                    new->next = NULL;
                    c->rc_channel_head = new;
                    if(new->buffer_size > c->rc_largest) {
                        c->rc_largest = new->buffer_size;
                    }
                } else {
                    mvdev_channel_rc * new = (mvdev_channel_rc *) srq_req->channel_ptr;
                    mvdev_channel_rc * curr = c->rc_channel_head;
                    mvdev_channel_rc * prev = NULL;
                    while(1) {
                        if(NULL == curr) {
                            prev->next = new;
                            new->next = NULL;
                            break;
                        } else if(new->buffer_size < curr->buffer_size) {
                            if(prev != NULL) {
                                new->next = prev->next;
                                prev->next = new;
                            } else {
                                c->rc_channel_head = new;
                            }
                            break;
                        }

                        prev = curr;
                        curr = curr->next;
                    }
                    if(new->buffer_size > c->rc_largest) {
                        c->rc_largest = new->buffer_size;
                    }
                }
                break;
            }
        case MVDEV_CH_RC_RQ:
            {
                mvdev_channel_request_rq * rq_req =
                    (mvdev_channel_request_rq *) req;
                c->rc_enabled = 1;
                c->rc_channel_head = (mvdev_channel_rc *) rq_req->channel_ptr;

                break;
            }
#ifdef XRC
        case MVDEV_CH_XRC_SHARED_MULT_SRQ:
            {
                /* look through each of the connections that share this
                 * QP and see if they need to be enabled
                 */
                mvdev_channel_request_xrc * xrc_req =
                    (mvdev_channel_request_xrc *) req;
                mvdev_channel_xrc_shared * shared = 
                    (mvdev_channel_xrc_shared *) xrc_req->channel_ptr;


                shared->status = MV_XRC_CONN_SHARED_ESTABLISHED;

                if(!shared->is_xtra) {
                    mvdev_connection_t * curr_conn = shared->xrc_channel_ptr;
                    while(NULL != curr_conn) {
                        if(MV_XRC_CONN_SINGLE_SRQN_RECEIVED == 
                                curr_conn->xrc_channel.status) {

                            curr_conn->xrc_channel.status = MV_XRC_CONN_SINGLE_ESTABLISHED;
                            curr_conn->xrc_enabled = 1;
                            if(curr_conn->rcfp_send_enabled_xrc_pending) {
                                MV_Recv_RC_FP_Addr(curr_conn, curr_conn->pending_rcfp);
                            }
                        }
                        curr_conn = curr_conn->xrc_channel_ptr; 
                    }  

                }

                D_PRINT("Exit activate MVDEV_CH_XRC_SHARED_MULT_SRQ\n");

                break;
            }
        case MVDEV_CH_XRC_SRQN:
            {
                /* if the shared XRC channel is ready, we can 
                 * mark ourselves as ready now (since we have the 
                 * SRQN). 
                 *
                 * If it isn't ready, we will need to just wait until
                 * the XRC qp is ready
                 */
                int i, j;
                mvdev_channel_xrc_shared * shared = c->xrc_channel.shared_channel;
                mvdev_packet_conn_pkt_srqn * srqn_pkt = (mvdev_packet_conn_pkt_srqn *) pkt;
                mvdev_srqn_remote * last = (mvdev_srqn_remote *) 
                    malloc(sizeof(mvdev_srqn_remote));
                mvdev_srqn_remote * current;

                D_PRINT("MVDEV_CH_XRC_SRQN: Activate (%p)\n", shared);

                /* we can setup all of the remote srqn information */

                last->buffer_size = srqn_pkt->buffer_size[0];
                for(j = 0; j < MAX_NUM_HCAS; j++) {
                    last->srqn[j] = srqn_pkt->srqn[j][0];
                }
                last->next = last->prev = NULL;
                c->xrc_channel.remote_srqn_head = c->xrc_channel.remote_srqn_tail = last;
                c->rc_largest = srqn_pkt->buffer_size[0];

                for(i = 1; i < srqn_pkt->num_srqs; i++) {
                    for(j = 0; j < MAX_NUM_HCAS; j++) {
                        current = (mvdev_srqn_remote *) 
                            malloc(sizeof(mvdev_srqn_remote));

                        current->buffer_size = srqn_pkt->buffer_size[i];
                        for(j = 0; j < MAX_NUM_HCAS; j++) {
                            current->srqn[j] = srqn_pkt->srqn[j][i];
                        }

                        if(current->buffer_size > c->rc_largest) {
                            c->rc_largest = current->buffer_size;
                        }

                        last->next = current;
                        current->prev = last;
                        current->next = NULL;
                        last = current;
                        c->xrc_channel.remote_srqn_tail = last;
                    }
                }

                if(MV_XRC_CONN_SHARED_ESTABLISHED == shared->status) {
                    c->xrc_channel.status = MV_XRC_CONN_SINGLE_ESTABLISHED;
                    c->xrc_enabled = 1;
                    if(c->rcfp_send_enabled_xrc_pending) {
                        MV_Recv_RC_FP_Addr(c, c->pending_rcfp);
                    }
                } else {
                    c->xrc_channel.status = MV_XRC_CONN_SINGLE_SRQN_RECEIVED;
                }


                break;
            }
#endif
        default:
            error_abort_all(GEN_EXIT_ERR, "Invalid type on activate (%d)\n", req->req_type);
    }


    /* should activate the channel */
}

void MV_Channel_Request_Remove(mvdev_connection_t * c, mvdev_channel_request * req) 
{

    if(NULL == req->prev) {
        MV_ASSERT(req == c->channel_request_head);
        c->channel_request_head = req->next;
    } else {
        req->prev->next = req->next;
    }

    if(NULL == req->next) {
        MV_ASSERT(req == c->channel_request_tail);
        c->channel_request_tail = req->prev;
    } else {
        req->next->prev = req->prev;
    }

    free(req);
}

void MV_Channel_Request_Add(mvdev_connection_t * c, mvdev_channel_request * req) 
{

    req->next = req->prev = NULL;

    if(NULL != c->channel_request_tail) {
        c->channel_request_tail->next = req; 
        req->prev = c->channel_request_tail;
    } else {
        c->channel_request_head = req;
    }
    c->channel_request_tail = req;

}

mvdev_channel_request * MV_Channel_Get_Request(
        mvdev_connection_t * c, int buf_size, int type) 
{

    mvdev_channel_request * ptr = c->channel_request_head;
    while(NULL != ptr) {
        if(type == ptr->req_type && 
                buf_size == ((mvdev_channel_request_rq *) ptr)->setup_rq.buf_size) {
            return ptr;
        }
        ptr = ptr->next;
    }
    return NULL; 
}

int MV_Channel_Check_Existing(mvdev_connection_t * c, int buf_size, int type) 
{
    int found = 0;

    /* first we need to make sure that we haven't already set this up */
    if(MVDEV_CH_RC_SRQ == type || MVDEV_CH_RC_RQ == type) {
        mvdev_channel_rc * ptr = c->rc_channel_head;
        while(NULL != ptr) {
            if(((buf_size == -1) || (ptr->buffer_size == buf_size)) && 
                    type == ptr->type) {
                found = 1;
                break;
            }
            ptr = ptr->next;
        }
    }

    /* check that we haven't already had a request for it */
    {
        mvdev_channel_request * ptr = c->channel_request_head;
        while(NULL != ptr) {
            if(type == ptr->req_type && 
                    buf_size == ((mvdev_channel_request_rq *) ptr)->setup_rq.buf_size) {
                found = 1;
                break;
            }
            ptr = ptr->next;
        }
    }

    return found;
}

mvdev_channel_request_srq * MV_Alloc_Req_SRQ_RC(mvdev_connection_t * c, int buf_size) 
{
    mvdev_channel_request_srq * req = (mvdev_channel_request_srq *)
        malloc(sizeof(mvdev_channel_request_srq));
    mvdev_channel_rc * ch = MV_Setup_RC_SRQ(c, buf_size);

    req->req_type = MVDEV_CH_RC_SRQ;
    req->status = MVDEV_CH_STATUS_NULL;
    req->channel_ptr = (void *) ch;
    req->rank = c->global_rank;

    req->setup_srq[0].hca_num = 0;
    req->setup_srq[0].buf_size = buf_size;
    /* TODO: change to whatever we want */
    req->setup_srq[0].inline_size = 128;
    req->setup_srq[0].setup_info[0].lid =
        mvdev.default_hca->default_port_attr->lid;
    req->setup_srq[0].setup_info[0].qpn = ch->qp[0].qp->qp_num;

    MV_Channel_Request_Add(c, (mvdev_channel_request *) req);

    return req;
}

#ifdef XRC
mvdev_channel_request_xrc * MV_Alloc_Req_XRC(mvdev_connection_t * c) 
{
    mvdev_channel_request_xrc * req = (mvdev_channel_request_xrc *)
        malloc(sizeof(mvdev_channel_request_xrc));
    mvdev_channel_xrc_shared * ch;

    req->req_type = MVDEV_CH_XRC_SHARED_MULT_SRQ;
    req->status = MVDEV_CH_STATUS_NULL;
    req->rank = c->global_rank;

    /* we need to allocate the XRC QP */
    ch = req->channel_ptr = (void *) MV_Setup_XRC_Shared(c);

    req->setup_rq.hca_num = 0;
    req->setup_rq.buf_size = -1;
    req->setup_rq.inline_size = 128;
    req->setup_rq.credits = -1;
    req->setup_rq.setup_info[0].lid =
        mvdev.default_hca->default_port_attr->lid;
    req->setup_rq.setup_info[0].qpn = ch->qp[0].qp->qp_num;

    MV_Channel_Request_Add(c, (mvdev_channel_request *) req);

    return req;
}

mvdev_channel_request_srqn * MV_Alloc_Req_XRC_SRQN(mvdev_connection_t * c) 
{
    mvdev_channel_request_srqn * req = (mvdev_channel_request_srqn *)
        malloc(sizeof(mvdev_channel_request_srqn));

    req->req_type = MVDEV_CH_XRC_SRQN;
    req->status = MVDEV_CH_STATUS_NULL;
    req->rank = c->global_rank;

    MV_Channel_Request_Add(c, (mvdev_channel_request *) req);

    return req;
}

/* At the moment we're just going to do all buffer sizes or none */
void MV_Channel_Connect_XRC(mvdev_connection_t * c) 
{

    mvdev_channel_xrc_shared * shared = c->xrc_channel.shared_channel;

    switch(shared->status) {
        case MV_XRC_CONN_SHARED_INIT:
            {
                mvdev_channel_request * req;
                req = (mvdev_channel_request *) MV_Alloc_Req_XRC(c);
                shared->status = MV_XRC_CONN_SHARED_CONNECTING;
                MV_Channel_Send_Request(c, req);
            }
        case MV_XRC_CONN_SHARED_CONNECTING:
        case MV_XRC_CONN_SHARED_ESTABLISHED:
            if(MV_XRC_CONN_SINGLE_INIT == c->xrc_channel.status) {
                c->xrc_channel.status = MV_XRC_CONN_SINGLE_SRQN_REQUESTED;
                MV_Channel_Send_Request(c, 
                        (mvdev_channel_request *) MV_Alloc_Req_XRC_SRQN(c));
            }
            break;
        default:
            error_abort_all(GEN_EXIT_ERR, "Invalid XRC shared status\n");
    }

}
#endif

void MV_Channel_Connect_SRQ_RC(mvdev_connection_t * c, int buf_size) 
{

    /* if it isn't already in progress or already setup 
     * then setup the connection and send the message back
     */
    if(!c->alloc_srq && !MV_Channel_Check_Existing(c, buf_size, MVDEV_CH_RC_SRQ)) {
        c->alloc_srq = 1;

        MV_Channel_Send_Request(c, 
                (mvdev_channel_request *) MV_Alloc_Req_SRQ_RC(c, buf_size));
    }
}

mvdev_channel_request_rq * MV_Alloc_Req_RQ_RC(mvdev_connection_t * c, 
        int buf_size, int credit) 
{
    mvdev_channel_request_rq * req = (mvdev_channel_request_rq *)
        malloc(sizeof(mvdev_channel_request_rq));
    mvdev_channel_rc * ch = MV_Setup_RC_RQ(c, buf_size, credit);

    req->req_type = MVDEV_CH_RC_RQ;
    req->status = MVDEV_CH_STATUS_NULL;
    req->rank = c->global_rank;
    req->channel_ptr = (void *) ch;

    req->setup_rq.hca_num = 0;
    req->setup_rq.buf_size = buf_size;
    /* TODO: change to whatever we want */
    req->setup_rq.inline_size = 128;
    req->setup_rq.credits = credit;
    req->setup_rq.setup_info[0].lid =
        mvdev.default_hca->default_port_attr->lid;
    req->setup_rq.setup_info[0].qpn = ch->qp[0].qp->qp_num;

    MV_Channel_Request_Add(c, (mvdev_channel_request *) req);

    return req;
}

void MV_Channel_Connect_RQ_RC(mvdev_connection_t * c, int buf_size, int credit) 
{
    if(!MV_Channel_Check_Existing(c, buf_size, MVDEV_CH_RC_RQ)) {
        MV_Channel_Send_Request(c, 
                (mvdev_channel_request *) MV_Alloc_Req_RQ_RC(c, buf_size, credit));
    }
}

void MV_Channel_Send_Request(mvdev_connection_t * c,  mvdev_channel_request * req) 
{
    MV_ASSERT(MVDEV_CH_STATUS_NULL == req->status);

    req->initiator_request_id = req;
    req->target_request_id = 0;

    mv_sbuf * v = get_sbuf_control(c, 
            MAX(sizeof(mvdev_packet_conn_pkt_srq), 
                sizeof(mvdev_packet_conn_pkt_rq)));
    v->rank = c->global_rank;

    switch(req->req_type) {
        case MVDEV_CH_RC_SRQ:
            {
                mvdev_channel_request_srq * srq_req = 
                    (mvdev_channel_request_srq *) req;
                mvdev_packet_conn_pkt_srq * p = 
                    (mvdev_packet_conn_pkt_srq *) v->header_ptr;

                PACKET_SET_HEADER(p, c, MVDEV_PACKET_CH_REQUEST);
                p->req_type = srq_req->req_type;
                p->initiator_request_id = req->initiator_request_id;
                p->target_request_id = 0;
                memcpy(&(p->srq_setup), &(srq_req->setup_srq), 
                        sizeof(mvdev_setup_srq) * MAX_SRQS);

                mvdev_post_channel_send(c, v, 
                        sizeof(mvdev_packet_conn_pkt_srq));
                break;
            }
        case MVDEV_CH_RC_RQ:
            {
                mvdev_channel_request_rq * rq_req = 
                    (mvdev_channel_request_rq *) req;
                mvdev_packet_conn_pkt_rq * p = 
                    (mvdev_packet_conn_pkt_rq *) v->header_ptr;

                PACKET_SET_HEADER(p, c, MVDEV_PACKET_CH_REQUEST);
                p->req_type = rq_req->req_type;
                p->initiator_request_id = req->initiator_request_id;
                p->target_request_id = 0;
                memcpy(&(p->rq_setup), &(rq_req->setup_rq), 
                        sizeof(mvdev_setup_rq));

                mvdev_post_channel_send(c, v, 
                        sizeof(mvdev_packet_conn_pkt_rq));
                break;
            }
#ifdef XRC
        case MVDEV_CH_XRC_SHARED_MULT_SRQ:
            {
                mvdev_channel_request_xrc * xrc_req = 
                    (mvdev_channel_request_xrc *) req;
                mvdev_packet_conn_pkt_xrc * p = 
                    (mvdev_packet_conn_pkt_xrc *) v->header_ptr;

                PACKET_SET_HEADER(p, c, MVDEV_PACKET_CH_REQUEST);
                p->req_type = req->req_type;
                p->initiator_request_id = req->initiator_request_id;
                p->target_request_id = 0;
                memcpy(&(p->rq_setup), &(xrc_req->setup_rq), 
                        sizeof(mvdev_setup_rq));

                mvdev_post_channel_send(c, v, 
                        sizeof(mvdev_packet_conn_pkt_xrc));
                break;
            }
        case MVDEV_CH_XRC_SRQN:
            {
                mvdev_packet_conn_pkt_srqn * p = 
                    (mvdev_packet_conn_pkt_srqn *) v->header_ptr;

                PACKET_SET_HEADER(p, c, MVDEV_PACKET_CH_REQUEST);
                p->req_type = req->req_type;
                p->initiator_request_id = req->initiator_request_id;

                p->target_request_id = 0;

                mvdev_post_channel_send(c, v, 
                        sizeof(mvdev_packet_conn_pkt_srqn));

                break;
            }
#endif
        case MVDEV_CH_RC_FP:
            {
                break;
            }
        default:
            error_abort_all(GEN_EXIT_ERR, "Invalid Request send reply type\n");
            break;

    }

    req->status = MVDEV_CH_STATUS_REQUESTED;
}



void MV_Channel_Send_Reply(mvdev_connection_t * c,  mvdev_channel_request * req) 
{
    mv_sbuf * v = get_sbuf_control(c, 
            MAX(sizeof(mvdev_packet_conn_pkt_srq), 
                sizeof(mvdev_packet_conn_pkt_rq)));
    v->rank = c->global_rank;

    switch(req->req_type) {
        case MVDEV_CH_RC_SRQ:
            {
                mvdev_channel_request_srq * srq_req = 
                    (mvdev_channel_request_srq *) req;
                mvdev_packet_conn_pkt_srq * p = 
                    (mvdev_packet_conn_pkt_srq *) v->header_ptr;

                PACKET_SET_HEADER(p, c, MVDEV_PACKET_CH_REPLY);
                p->req_type = srq_req->req_type;
                p->target_request_id = req->target_request_id;
                p->initiator_request_id = req->initiator_request_id;
                memcpy(&(p->srq_setup), &(srq_req->setup_srq), 
                        sizeof(mvdev_setup_srq) * MAX_SRQS);

                mvdev_post_channel_send(c, v, 
                        sizeof(mvdev_packet_conn_pkt_srq));
                break;
            }
        case MVDEV_CH_RC_RQ:
            {
                mvdev_channel_request_rq * rq_req = 
                    (mvdev_channel_request_rq *) req;
                mvdev_packet_conn_pkt_rq * p = 
                    (mvdev_packet_conn_pkt_rq *) v->header_ptr;

                PACKET_SET_HEADER(p, c, MVDEV_PACKET_CH_REPLY);
                p->req_type = rq_req->req_type;
                p->target_request_id = req->target_request_id;
                p->initiator_request_id = req->initiator_request_id;
                memcpy(&(p->rq_setup), &(rq_req->setup_rq), 
                        sizeof(mvdev_setup_rq));

                mvdev_post_channel_send(c, v, 
                        sizeof(mvdev_packet_conn_pkt_rq));
                break;
            }
#ifdef XRC
        case MVDEV_CH_XRC_SHARED_MULT_SRQ:
            {
                mvdev_channel_request_xrc * xrc_req = 
                    (mvdev_channel_request_xrc *) req;
                mvdev_packet_conn_pkt_xrc * p = 
                    (mvdev_packet_conn_pkt_xrc *) v->header_ptr;

                PACKET_SET_HEADER(p, c, MVDEV_PACKET_CH_REPLY);
                p->req_type = xrc_req->req_type;
                p->target_request_id = req->target_request_id;
                p->initiator_request_id = req->initiator_request_id;
                memcpy(&(p->rq_setup), &(xrc_req->setup_rq), 
                        sizeof(mvdev_setup_rq));

                mvdev_post_channel_send(c, v, 
                        sizeof(mvdev_packet_conn_pkt_xrc));
                break;
            }
        case MVDEV_CH_XRC_SRQN:
            {
                int i = 0, j = 0;

                mvdev_channel_request_srqn * srqn_req = 
                    (mvdev_channel_request_srqn *) req;
                mvdev_packet_conn_pkt_srqn * p = 
                    (mvdev_packet_conn_pkt_srqn *) v->header_ptr;

                PACKET_SET_HEADER(p, c, MVDEV_PACKET_CH_REPLY);
                p->req_type = srqn_req->req_type;
                p->target_request_id = req->target_request_id;
                p->initiator_request_id = req->initiator_request_id;
                p->num_srqs = srqn_req->num_srqs;

                for(i = 0; i < MAX_NUM_HCAS; i++) {
                    for(j = 0; j < MAX_SRQS; j++) {
                        p->srqn[i][j] = srqn_req->srqn[i][j];
                    }
                }
                for(j = 0; j < MAX_SRQS; j++) {
                    p->buffer_size[j] = srqn_req->buffer_size[j];
                }

                mvdev_post_channel_send(c, v, 
                        sizeof(mvdev_packet_conn_pkt_srqn));
            }
#endif
        case MVDEV_CH_RC_FP:
            {
                break;
            }
        default:
            error_abort_all(GEN_EXIT_ERR, "Invalid Channel send reply type\n");
            break;

    }

    req->status = MVDEV_CH_STATUS_REPLIED;
}

void MV_Channel_Send_Ack(mvdev_connection_t * c,  mvdev_channel_request * req) 
{
    mv_sbuf * v;
    mvdev_packet_conn_pkt * p;

    /* TODO: send over channel just created! */
    switch(req->req_type) {
        case MVDEV_CH_XRC_SHARED_MULT_SRQ:
            v = get_sbuf_control(c, sizeof(mvdev_packet_conn_pkt));
            p = (mvdev_packet_conn_pkt *) v->header_ptr;
            break;
        default:
            v = get_sbuf_control(c, sizeof(mvdev_packet_conn_pkt));
            p = (mvdev_packet_conn_pkt *) v->header_ptr;
            break;
    }

    v->rank = c->global_rank;

    PACKET_SET_HEADER(p, c, MVDEV_PACKET_CH_ACK);

    p->target_request_id = req->target_request_id;
    p->initiator_request_id = req->initiator_request_id;
    p->req_type = req->req_type;

    mvdev_post_channel_send(c, v, sizeof(mvdev_packet_conn_pkt));
}


void MV_Channel_Transition_SRQ_RC(mvdev_connection_t * c, 
        mvdev_channel_request_srq * local_req,
        mvdev_packet_conn_pkt_srq * incoming_req) 
{
    mvdev_channel_rc * ch = (mvdev_channel_rc *) local_req->channel_ptr;

    /* TODO: eventually support multiple HCAs */
    MV_Transition_RC_QP(ch->qp[0].qp, incoming_req->srq_setup[0].setup_info[0].qpn, 
            incoming_req->srq_setup[0].setup_info[0].lid, 1);
    ch->qp[0].remote_qpn = incoming_req->srq_setup[0].setup_info[0].qpn;
}

void MV_Channel_Transition_RQ_RC(mvdev_connection_t * c, 
        mvdev_channel_request_rq * local_req,
        mvdev_packet_conn_pkt_rq * incoming_req) 
{
    mvdev_channel_rc * ch = (mvdev_channel_rc *) local_req->channel_ptr;

    /* TODO: eventually support multiple HCAs */
    MV_Transition_RC_QP(ch->qp[0].qp, incoming_req->rq_setup.setup_info[0].qpn, 
            incoming_req->rq_setup.setup_info[0].lid, 1);
}


#ifdef XRC
void MV_Channel_Transition_XRC(mvdev_connection_t * c, 
        mvdev_channel_request_xrc * local_req,
        mvdev_packet_conn_pkt_xrc * incoming_req) 
{
    mvdev_channel_xrc_shared * ch = (mvdev_channel_xrc_shared *) local_req->channel_ptr;

    /* TODO: eventually support multiple HCAs */
    MV_Transition_XRC_QP(ch->qp[0].qp, incoming_req->rq_setup.setup_info[0].qpn, 
            incoming_req->rq_setup.setup_info[0].lid, 1);
}
#endif


/*
 * Initiator: Send request
 * Target : Receive Request, Send Reply
 * Initiator: Receive reply, send ack
 * Target: Receive Ack
 */

void MV_Channel_Incoming_Ack(mvdev_connection_t * c, mvdev_packet_conn_pkt * req) 
{
    MV_Channel_Activate(c, (mvdev_channel_request *) req->target_request_id, req);
    MV_Channel_Request_Remove(c, (mvdev_channel_request *) req->target_request_id);
}


void MV_Channel_Incoming_Request(mvdev_connection_t * c, mvdev_packet_conn_pkt * req) 
{

    D_PRINT("Got incoming request from %d, type: %d, initiator req_id: %p\n", 
            c->global_rank, req->req_type, req->initiator_request_id);

    switch(req->req_type) {
        case MVDEV_CH_RC_SRQ:
            {
                mvdev_packet_conn_pkt_srq * p = (mvdev_packet_conn_pkt_srq *) req;
                mvdev_channel_request_srq * req_srq = 
                    (mvdev_channel_request_srq *) 
                    MV_Channel_Get_Request(c, p->srq_setup[0].buf_size, 
                            MVDEV_CH_RC_SRQ);

                c->alloc_srq = 1;

                if(NULL == req_srq) {
                    req_srq = MV_Alloc_Req_SRQ_RC(c, p->srq_setup[0].buf_size);
                } else if(mvdev.me > c->global_rank) {
                    break;
                } 

                req_srq->target_request_id = req_srq;
                req_srq->initiator_request_id = req->initiator_request_id;

                MV_Channel_Transition_SRQ_RC(c, req_srq, p);
                MV_Channel_Send_Reply(c, (mvdev_channel_request *) req_srq);

                break;
            }
        case MVDEV_CH_RC_RQ:
            {
                mvdev_packet_conn_pkt_rq * p = (mvdev_packet_conn_pkt_rq *) req;
                mvdev_channel_request_rq * req_rq = 
                    (mvdev_channel_request_rq *) 
                    MV_Channel_Get_Request(c, p->rq_setup.buf_size, 
                            MVDEV_CH_RC_RQ);

                if(NULL == req_rq) {
                    req_rq = MV_Alloc_Req_RQ_RC(c, 
                            p->rq_setup.buf_size, p->rq_setup.credits);
                } else if(mvdev.me > c->global_rank) {
                    break;
                } 
                req_rq->target_request_id = req_rq;
                req_rq->initiator_request_id = req->initiator_request_id;

                MV_Channel_Transition_RQ_RC(c, req_rq, p);
                MV_Channel_Send_Reply(c, (mvdev_channel_request *) req_rq);

                break;
            }
#ifdef XRC
        case MVDEV_CH_XRC_SHARED_MULT_SRQ: 
            {
                mvdev_packet_conn_pkt_xrc * p = (mvdev_packet_conn_pkt_xrc *) req;
                mvdev_channel_request_xrc * req_xrc = 
                    (mvdev_channel_request_xrc *) 
                    MV_Channel_Get_Request(c, p->rq_setup.buf_size, 
                            req->req_type);

                if(NULL == req_xrc) {
                    mvdev_channel_xrc_shared * shared = c->xrc_channel.shared_channel;

                    req_xrc = MV_Alloc_Req_XRC(c);

                    /* we need to now mark this as in progress */
                    if(MV_XRC_CONN_SHARED_INIT == shared->status) {
                        shared->status = MV_XRC_CONN_SHARED_CONNECTING;
                    }
                
                } else if(mvdev.me > c->global_rank) {
                    D_PRINT("throwing away from %d\n", c->global_rank);
                    break;
                } 

                req_xrc->target_request_id = req_xrc;
                req_xrc->initiator_request_id = req->initiator_request_id;

                MV_Channel_Transition_XRC(c, req_xrc, p);
                MV_Channel_Send_Reply(c, (mvdev_channel_request *) req_xrc);

                break;
            }
        case MVDEV_CH_XRC_SRQN:
            {
                int i = 0, j = 0;
                mv_srq_shared * s = mvdev.xrc_srqs;
                mvdev_channel_request_srqn * req_srqn = MV_Alloc_Req_XRC_SRQN(c);

                D_PRINT("Got MVDEV_CH_XRC_SRQN Request\n");

                req_srqn->target_request_id = req_srqn;
                req_srqn->initiator_request_id = req->initiator_request_id;

                while(NULL != s) {
                    for(i = 0; i < mvdev.num_hcas; i++) {
                        req_srqn->srqn[i][j] = 
                            s->pool[i]->srq->srq->xrc_srq_num;
                    }
                    req_srqn->buffer_size[j] = s->pool[0]->buffer_size;

                    s = s->next;
                    j++;
                }
                req_srqn->num_srqs = j;

                MV_Channel_Send_Reply(c, (mvdev_channel_request *) req_srqn);
                MV_Channel_Request_Remove(c, (mvdev_channel_request *) req_srqn);

                break;
            }
#endif
        default:
            error_abort_all (GEN_EXIT_ERR, "Invalid Channel request type\n");
            break;
    }
}

void MV_Channel_Incoming_Reply(mvdev_connection_t * c, 
        mvdev_packet_conn_pkt * reply) 
{

    /* look up what request this reply is for... */
    mvdev_channel_request * local_req = 
        (mvdev_channel_request *) reply->initiator_request_id;

    if(reply->req_type != local_req->req_type) {
        MV_ASSERT(reply->req_type == local_req->req_type);
    }

    /* keep track of the target id */
    local_req->target_request_id = reply->target_request_id;

    switch(reply->req_type) {
        case MVDEV_CH_RC_SRQ:
            {
                MV_Channel_Transition_SRQ_RC(c, 
                        (mvdev_channel_request_srq *) local_req, 
                        (mvdev_packet_conn_pkt_srq *) reply);
                MV_Channel_Send_Ack(c, local_req);

                /* mark this channel as ready */
                MV_Channel_Activate(c, local_req, reply);
                MV_Channel_Request_Remove(c, local_req);

                break;
            }
        case MVDEV_CH_RC_RQ:
            {
                MV_Channel_Transition_RQ_RC(c, 
                        (mvdev_channel_request_rq *) local_req, 
                        (mvdev_packet_conn_pkt_rq *) reply);
                MV_Channel_Send_Ack(c, local_req);

                /* mark this channel as ready */
                MV_Channel_Activate(c, local_req, reply);
                MV_Channel_Request_Remove(c, local_req);

                break;
            }
#ifdef XRC
        case MVDEV_CH_XRC_SHARED_MULT_SRQ: 
            {
                MV_Channel_Transition_XRC(c, 
                        (mvdev_channel_request_rq *) local_req, 
                        (mvdev_packet_conn_pkt_rq *) reply);

                MV_Channel_Send_Ack(c, local_req);

                /* mark this channel as ready */
                MV_Channel_Activate(c, local_req, reply);
                MV_Channel_Request_Remove(c, local_req);

                break;
            }
        case MVDEV_CH_XRC_SRQN:
            {
                MV_Channel_Activate(c, local_req, reply);
                MV_Channel_Request_Remove(c, local_req);

                break;
            }
#endif
        default:
            error_abort_all (GEN_EXIT_ERR, "Invalid Channel reply type\n");
            break;
    }
}


