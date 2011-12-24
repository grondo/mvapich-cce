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
/* Copyright (c) 2008, Mellanox Technologies. All rights reserved. */

#include "mpid.h"
#include "ibverbs_header.h"
#include "viadev.h"
#include "viapriv.h"
#include "cm_user.h"
#include "nfr.h"

int nfr_max_failures = NFR_MAX_FAILURES;
volatile int nfr_fatal_error = 0; /* 0 - no fatal, 1 - fatal happend. The fatal may be enabled only in async thread*/
int nfr_timeout_on_error = NFR_DEFAULT_TIMEOUT_ON_ERROR;
int nfr_timeout_on_restart = NFR_DEFAULT_TIMEOUT_ON_RESTART;

int nfr_num_of_bad_connections = 0;
int nfr_wait_for_replies = 0;

/*********************************************************************/
/* Local function. Used only in this file                            */
/*********************************************************************/

static void send_reconnect_req(viadev_connection_t *c)
{
    V_PRINT(DEBUG03, "NFR: Send reconnect request to [%d]\n", c->global_rank);
    vbuf *v = get_vbuf();
    viadev_packet_noop *p = (viadev_packet_noop *) VBUF_BUFFER_START(v);
    PACKET_SET_HEADER_NFR_REQ(p, c);
    vbuf_init_send(v, sizeof(viadev_packet_header));
    viadev_post_send(c, v);
}

static void send_reconnect_rep(viadev_connection_t *c)
{
    V_PRINT(DEBUG03, "NFR: Send reconnect reply to [%d]\n", c->global_rank);
    vbuf *v = get_vbuf();
    viadev_packet_noop *p = (viadev_packet_noop *) VBUF_BUFFER_START(v);
    PACKET_SET_HEADER_NFR_REP(p, c);
    vbuf_init_send(v, sizeof(viadev_packet_header));
    viadev_post_send(c, v);
}

static void send_reconnect_fin(viadev_connection_t *c)
{
    V_PRINT(DEBUG03, "NFR: Send reconnect fin to [%d]\n", c->global_rank);
    vbuf *v = get_vbuf();
    viadev_packet_noop *p = (viadev_packet_noop *) VBUF_BUFFER_START(v);
    PACKET_SET_HEADER_NFR_FIN(p, c);
    vbuf_init_send(v, sizeof(viadev_packet_header));
    viadev_post_send(c, v);
}

static void restore_cached(vbuf * v)
{
    void *tmp_buffer, *databuf;

    int len, buf_len;
    packet_sequence_t id;
    VBUF_FLAG_TYPE flag;
    viadev_packet_eager_start *h = 
        (viadev_packet_eager_start *)VBUF_BUFFER_START(v);
    
    V_PRINT(DEBUG03,"Restore_cached start id - %d\n", h->header.id);
    /* Save id */
    id   = h->header.id;                   
    /* len */
    len = h->header.fast_eager_size;
    buf_len = sizeof(viadev_packet_eager_start) + len;
#ifdef _IA64_
    buf_len += 16;
    buf_len -= (len & 7);
#endif

    tmp_buffer = (void*)malloc(len);
    if (NULL == tmp_buffer) {
        error_abort_all(IBV_RETURN_ERR, "Failed to allocate memory for tmp_buffer");
    }
    databuf = ((char *) h) + FAST_EAGER_HEADER_SIZE;
    memcpy(tmp_buffer, databuf, len);
    
    h->envelope.context     = v->match_hdr.envelope.context;                                  
    h->envelope.tag         = v->match_hdr.envelope.tag;                                  
    h->envelope.data_length = v->match_hdr.envelope.data_length;                                  
    h->envelope.src_lrank   = v->match_hdr.envelope.src_lrank;                                  

    /* Restore header */
    h->header.type = VIADEV_PACKET_EAGER_START;
    h->header.src_rank = viadev.me;
    h->header.id = id;
    h->bytes_in_this_packet = len;
    /* Credits will be reseted later in re_post_send */
    /* Restore data buffer */
    databuf = ((char *) h) + sizeof(viadev_packet_eager_start);
    memcpy(databuf, tmp_buffer, len);

    /* Restore flags */
    if ((int) *(VBUF_FLAG_TYPE *) (v->buffer + buf_len) == buf_len) {
        flag = (VBUF_FLAG_TYPE) (buf_len + FAST_RDMA_ALT_TAG);
    } else {
        flag = (VBUF_FLAG_TYPE) buf_len;
    }

    /* set head flag */
    *(v->head_flag) = flag;
    /* set tail flag */
    *(VBUF_FLAG_TYPE *) (v->buffer + buf_len) = flag;

    /* set ib describtor */
    v->desc.sg_entry.length =
        buf_len + VBUF_FAST_RDMA_EXTRA_BYTES;

#ifdef _IBM_EHCA_
    /* Inline Data transfer not supported on IBM
     * EHCA */    
    v->desc.u.sr.send_flags =
            IBV_SEND_SIGNALED;
#else
    if(v->desc.sg_entry.length < viadev.connections[viadev.me].max_inline) {
        v->desc.u.sr.send_flags =
            IBV_SEND_SIGNALED | IBV_SEND_INLINE;
    } else {
        v->desc.u.sr.send_flags =
            IBV_SEND_SIGNALED;
    }
#endif

    V_PRINT(DEBUG03,"Restore header DONE - id[%d] src[%d] type[%s] tag[%d] src_lrank[%d] len[%d]\n"
            ,h->header.id, h->header.src_rank, type2name(h->header.type), h->envelope.tag, 
            h->envelope.src_lrank, h->envelope.data_length);
}

#define GET_RNDV_PROTOCOL(v) ((viadev_packet_rendezvous_start*)VBUF_BUFFER_START(v))->protocol

static void re_post_send(viadev_connection_t * c, vbuf * v)
{
    struct ibv_send_wr *bad_wr;
    viadev_packet_header *p;
   
    p = (viadev_packet_header *) VBUF_BUFFER_START(v);
   
    V_PRINT(DEBUG03,"ReSending %d, id %d\n", p->src_rank, p->id);
    /* pasha */

    if (VIADEV_PACKET_RENDEZVOUS_START == VBUF_TYPE(v) && 
                VIADEV_PROTOCOL_R3 != GET_RNDV_PROTOCOL(v)) {
                viadev_packet_rendezvous_start *packet = (viadev_packet_rendezvous_start *)VBUF_BUFFER_START(v);
                V_PRINT(DEBUG03, "Rdma start rkey %d\n", packet->memhandle_rkey);
    }


    if (FAST_EAGER_CACHED == p->type) {
        /* uff - now we need restore header ! */
        restore_cached(v);
    }

    /* reset ack status in the packet */
    NFR_SET_ACK(p, c);
    v->ib_completed = 0; /* must reset ib completion status */

    if(viadev_use_srq) {
        PACKET_SET_CREDITS(p, c);

        if(c->send_wqes_avail <= 0) {
            viadev_ext_sendq_queue(c, v);
            return;
        }

        c->send_wqes_avail--;

        V_PRINT(DEBUG03,"reposting %d -> %d, id %d qp %x \n", p->src_rank, c->global_rank, p->id, c->vi->qp_num);
        if(ibv_post_send(c->vi, &(v->desc.u.sr), &bad_wr)) {
            error_abort_all(IBV_RETURN_ERR,"Error posting send\n");
        }

        pthread_spin_lock(&viadev.srq_post_spin_lock);

        if(viadev.posted_bufs <= viadev_credit_preserve) {
            viadev.posted_bufs += 
                viadev_post_srq_buffers(viadev_srq_fill_size - viadev.posted_bufs);
        }

        pthread_spin_unlock(&viadev.srq_post_spin_lock);

    } else {

        /* 
         * will need to grab lock in multithreaded 
         * will need to put sequence number in immediate data for unreliable
         * currently this will tell you if you're out of remote credit
         * but will not block or return error to caller.
         */

        if (c->remote_credit > 0 || p->type == VIADEV_PACKET_NOOP) {
            /* if we got here, the backlog queue better be  empty */
            assert(c->backlog.len == 0 || p->type == VIADEV_PACKET_NOOP);

            PACKET_SET_CREDITS(p, c);
            if (p->type != VIADEV_PACKET_NOOP)
                c->remote_credit--;
            v->grank = c->global_rank;

            if (c->send_wqes_avail <= 0) {
                viadev_ext_sendq_queue(c, v);
                return;
            }
            c->send_wqes_avail--;

            if(ibv_post_send(c->vi, &(v->desc.u.sr), &bad_wr)) {
                error_abort_all(IBV_RETURN_ERR,"Error posting send\n");
            }

            /* do post receive after post_sr for last part of message
             * so latency hidden during post_sr
             */
            if (viadev_prepost_threshold && v->shandle != NULL
                    && c->initialized) {
                int needed =
                    viadev_prepost_depth + viadev_prepost_noop_extra +
                    MIN(viadev_prepost_rendezvous_extra,
                            c->rendezvous_packets_expected);
                while (c->preposts < (int) viadev_rq_size && c->preposts < needed) {
                    PREPOST_VBUF_RECV(c);
                }
                viadev_send_noop_ifneeded(c);
            }
        } else {
            /* must delay the send until more credits arrive. */

            D_PRINT("NFR RE post_send QUEUED: [vi %d][%d %d %d][vbuf %p] %s\n",
                    c->global_rank, c->remote_credit, c->local_credit,
                    c->remote_cc, v, viadev_packet_to_string(c->vi, v));
        }

    }
}

#define GET_RNDV_PROTOCOL(v) ((viadev_packet_rendezvous_start*)VBUF_BUFFER_START(v))->protocol
/* This function drops eager messages and resends RNDVZ start messages*/
static void drop_or_resend_rndv_start(viadev_connection_t *c, vbuf *v)
{
    ack_list *list = &c->waiting_for_ack;
    
    if (VIADEV_PACKET_RENDEZVOUS_START != VBUF_TYPE(v) && 
                VIADEV_PACKET_RENDEZVOUS_START_RETRY != VBUF_TYPE(v)) {
        WAITING_LIST_REMOVE(list, v);
#ifdef ADAPTIVE_RDMA_FAST_PATH
        if (IS_NORMAL_VBUF(v)) {
            release_vbuf(v); /* otherway ib completen will release it */
        } else {
            /* Pasha: Is not needed for fast path nr_set_ib_completed(v); */
            v->padding = FREE_FLAG;
            v->len = 0;
        }
#else
        RELEASE_VBUF(v);
#endif
        V_PRINT(DEBUG01, "NFR drop and send: Released packet %s[%d][%s][%d] \n",
                type2name(VBUF_TYPE(v)), VBUF_TYPE(v), padding2name(v->padding), ((viadev_packet_header*)v->buffer)->id);
    } else if (VIADEV_PACKET_RENDEZVOUS_START == VBUF_TYPE(v) && 
             VIADEV_PROTOCOL_R3 == GET_RNDV_PROTOCOL(v)){
        /* do not do anything with rndv_start R3 messages, they will released later by rndv_reply from remote side*/
        return;
    } else {
        /* chenge the header */
        viadev_packet_header *h = (viadev_packet_header *) VBUF_BUFFER_START(v);
        h->type = VIADEV_PACKET_RENDEZVOUS_START_RETRY;
        /* reset ib complition */
        v->ib_completed = 0;
        assert(v->sw_completed == 0);
        /* If it was fatal we need register the buffer */
        {
            viadev_packet_rendezvous_start *packet = (viadev_packet_rendezvous_start*) VBUF_BUFFER_START(v);
            V_PRINT(DEBUG03,"RNDV RKEY %d\n", packet->memhandle_rkey);
        }
        /* resend randevouze start */ 
        re_post_send(c, v);

        V_PRINT(DEBUG01, "NFR drop and send: resend randevoze %s[%d][%s][%d] \n",
                type2name(VBUF_TYPE(v)), VBUF_TYPE(v), padding2name(v->padding), ((viadev_packet_header*)v->buffer)->id);
    }
}

#define GET_ID(v) ((viadev_packet_header *)VBUF_BUFFER_START(v))->id

/* Re-trasmit all packets that were NOT recived by remote peer */
static void send_lost_data(viadev_connection_t *c, packet_sequence_t last_recv)
{
    vbuf *v = NULL, 
         *v_next = NULL, 
         *stop = NULL;
    packet_sequence_t id;
    int first_is_bigger = 0;
    int done_with_rndv_ret = 0;
    int id_is_last = 0;
    ack_list *list = &c->waiting_for_ack;

    V_PRINT(DEBUG03, "NFR: Entering send_lost_data\n");
    V_PRINT(DEBUG03, "NFR: %d [next-%d,last-%d] packets should be retrassmited\n",
            c->next_packet_tosend - last_recv, c->next_packet_tosend, last_recv);

    if (WAITING_LIST_IS_EMPTY(list)) {
        /* this case mabe only if list fin Fin packet were lost */
        V_PRINT(DEBUG03, "NFR: List is EMPTY next %d, last %d", c->next_packet_tosend, last_recv);
        V_PRINT(DEBUG03, " reseting next %d, to %d\n", c->next_packet_tosend, last_recv);
        c->next_packet_tosend = last_recv; /* reset the next packet to send */
        goto lost_done;
    }

    if ( GET_ID(WAITING_LIST_GET_FIRST(list)) > last_recv) {
        V_PRINT(DEBUG03, "NFR: First is bigger !\n");
        first_is_bigger = 1;
    }
    
    /* Look for wrap arround point and check that we can recover */
    v_next = WAITING_LIST_GET_FIRST(list);
    while (v_next != WAITING_LIST_GET_END(list)) {
        v = v_next;
        v_next = v->next;

        id = GET_ID(v);
        /* 
         * first_is_bigger allow to us hadnle follow sequiense of id in waiting list:
         * last_recv = 4 and
         * list START-> 998 999 0 1 2 3 4 5 6 <-END
         */

        V_PRINT(DEBUG03, "NFR:Decide what to do with vbuf, id[%d] last_recv[%d] netxt_to_send[%d] len[%d]\n",
                id, last_recv, c->next_packet_tosend, WAITING_LIST_LEN(list));
        if ( 1 == first_is_bigger) {
            if ( id > last_recv) {
                /* Drop sends and resend RNDVZ request */
                drop_or_resend_rndv_start(c,v);
            } else {
                first_is_bigger = 0;
            }
        } 

        if ( 0 == first_is_bigger) {
            if ( id < last_recv) {
                /* Drop sends and resend RNDVZ request */
                drop_or_resend_rndv_start(c, v);
            } else {
                id_is_last = 1;               
                assert( id < c->next_packet_tosend);
                if (0 == done_with_rndv_ret) {
                    /* Send fin message that says that we done with rndv retransmit*/
                    send_reconnect_fin(c);
                    done_with_rndv_ret = 1;
                }
                /* Resend packets that where not recived by remote side */
                re_post_send(c, v);
            }
        } 
    }

    if (last_recv != c->next_packet_tosend) {
        if (id_is_last) {
            V_PRINT(DEBUG03, "Reseting next %d, to %d\n", c->next_packet_tosend, id + 1);
            c->next_packet_tosend = id + 1;
        } else {
            V_PRINT(DEBUG03, "Reseting next %d, to last %d\n", c->next_packet_tosend, last_recv);
            c->next_packet_tosend = last_recv;
        }
    }

lost_done:
    if (0 == done_with_rndv_ret) {
        /* Send fin message that says that we done with rndv retransmit*/
        send_reconnect_fin(c);
    }

    V_PRINT(DEBUG03, "NFR: Exit send_lost_data\n");
}

static void reconnect(viadev_connection_t *c)
{
    int peer = c->global_rank;
    /* reset MPI connection and CM connection status for CM reconnect */
    V_PRINT(DEBUG03, "NFR: increasing nfr_num_of_bad_connections st %d num %d\n",
            c->qp_status, nfr_num_of_bad_connections + 1);
    c->was_connected++;
    if (c->was_connected > nfr_max_failures) {
        error_abort_all(GEN_EXIT_ERR, "\nNumber of network failures between ranks %d[%s] and %d exceed maximal limit - %d\n"
                        "Please use ibdiagnet in order to debug your network issue\n"
                        "Stoping MPI process.........\n"
                        , viadev.me, viadev.my_name, c->global_rank, nfr_max_failures);
    }
    c->qp_status = QP_DOWN;
    /* Increase global counter of bad connections */
    nfr_num_of_bad_connections++;
    if (MPICM_Reconnect_req(peer) < 0) {
        error_abort_all(GEN_EXIT_ERR, "Failed to send reconnect request");
    }
}

static int process_bad_completion(struct ibv_wc *sc)
{
    viadev_connection_t *c = NULL;
    void *vbuf_addr = NULL;
    vbuf *v = NULL;
    int i; 

    assert(NULL != sc);

    /* Got completion with error  - process it */

    vbuf_addr = (void *) ((aint_t) sc->wr_id);
    v = (vbuf *)vbuf_addr;
    i = v->grank;
    c = &(viadev.connections[i]);
    V_PRINT(DEBUG03, "NFR: Processing completion with error rank[%d], addr[%p]\n", i, v);

    if ( IBV_WC_REM_ACCESS_ERR == sc->status) {
        while (1) {
        }
    }

    /* All other request will wait on pending lists */
    if (QP_UP == c->qp_status) {
        reconnect(c);
    }

    return i;
}

/* pool for data */
static int poll4data()
{
    int ret = 0, ofo, ne; 
    struct ibv_wc rc, sc;
    void *vbuf_addr;

    odu_test_new_connection(); /* Poll CM for new connection request */

#ifdef ADAPTIVE_RDMA_FAST_PATH /* Continu to progress as usual*/
    ret = poll_rdma_buffer(&vbuf_addr, &ofo);
    if (ret == 0) {
        viadev_process_recv(vbuf_addr);
    }
#endif
    ne = ibv_poll_cq(viadev.cq_hndl, 1, &sc);
    if (ne < 0) {
        error_abort_all(IBV_RETURN_ERR, "Error polling CQ\n");
    } else if (ne > 1) {
        error_abort_all(IBV_RETURN_ERR, "Asked only one completion, "
                "got more!\n");
    } else if (1 == ne) {
        vbuf_addr = (void *) ((aint_t) sc.wr_id);

        /* Need to check if it is a completion with error */
        if (sc.status != IBV_WC_SUCCESS) {
            NFR_PRINT("[%s:%d] Network Recovery: Got another completion with error %s, "
                    "code=%d, dest rank=%d - continue Network Recovery process\n",
                    viadev.my_name, viadev.me,
                    wc_code_to_str(sc.status), sc.status,
                    ((vbuf *) vbuf_addr)->grank);
            /* on each error we will re-reset the timer */
            process_bad_completion(&sc);
            return 1;
        }
        if (viadev_use_on_demand && sc.opcode == IBV_WC_RECV) {
            int rank = -1;
            vbuf *v = (vbuf *)vbuf_addr;
            viadev_packet_header *h = (viadev_packet_header *) (VBUF_BUFFER_START(v));
            viadev_connection_t *c = NULL;
#if !defined(DISABLE_HEADER_CACHING) && defined(ADAPTIVE_RDMA_FAST_PATH)
            if (h->type == FAST_EAGER_CACHED) {
                c = &(viadev.connections[v->grank]);
                rank = v->grank;
            } else {
                c = &(viadev.connections[h->src_rank]);
                rank = h->src_rank;
            }
#else
            c = &(viadev.connections[h->src_rank]);
            rank = h->src_rank;
#endif /* !DISABLE_HEADER_CACHING && ADAPTIVE_RDMA_FAST_PATH */
            if (c->next_packet_expected == 1) {
                /* First packet from this peer*/
                MPICM_Server_connection_establish(rank);
                if (viadev.pending_req_head[rank]) {
                    cm_process_queue(rank);
                }
            }
        }
        if (sc.opcode == IBV_WC_SEND ||
                sc.opcode == IBV_WC_RDMA_WRITE ||
                sc.opcode == IBV_WC_RDMA_READ) {
            viadev_process_send(vbuf_addr);
        } else {
            viadev_process_recv(vbuf_addr);
        }
    }
    return 0;
}

/* Go thtought waiting_for_ack and rndv_inprocess list and re-register memory */
static void reregister_lists()
{
    int i;
    MPIR_RHANDLE *rhandle = NULL;
    MPIR_SHANDLE *shandle = NULL;
    vbuf *v = NULL;
    viadev_packet_rendezvous_start *packet;
    
    for (i = 0; i < viadev.np; i++) {
        viadev_connection_t *c = &viadev.connections[i];
        ack_list *wlist = &c->waiting_for_ack;
        rndv_list *rlist = &c->rndv_inprocess;

        if (i == viadev.me || 1 != c->initialized) {
            continue;
        }
        
        for(v = WAITING_LIST_GET_FIRST(wlist); 
                    v != WAITING_LIST_GET_END(wlist);
                    v = v->next) {
            viadev_packet_header *header = (viadev_packet_header*) VBUF_BUFFER_START(v);
            if (VIADEV_PACKET_RENDEZVOUS_START == VBUF_TYPE(v) && 
                VIADEV_PROTOCOL_R3 != GET_RNDV_PROTOCOL(v)) {
                packet = (viadev_packet_rendezvous_start *)VBUF_BUFFER_START(v);
                shandle = (MPIR_SHANDLE*)ID_TO_REQ(packet->sreq);
                if (NULL != shandle->dreg_entry) {
                    /* register, update key */
                    dreg_entry *dreg_entry = NULL;
                    dreg_entry = dreg_register(packet->buffer_address, packet->len, DREG_ACL_READ);
                    if (NULL == dreg_entry) {
                        error_abort_all(IBV_RETURN_ERR,"NFR failed re-register memory during fatal recovery");
                    }
                    packet->memhandle_rkey = dreg_entry->memhandle->rkey;
                    shandle->dreg_entry = dreg_entry;
                    V_PRINT(DEBUG03, "Re-register addr: %x key: %d\n",packet->buffer_address, packet->memhandle_rkey);
                }
            }
        }

        for(rhandle = WAITING_LIST_GET_FIRST(rlist); 
                    rhandle != WAITING_LIST_GET_END(rlist);
                    rhandle = rhandle->next ) {
             if (NULL != rhandle->dreg_entry) {
                 /* register, update key */
                 viadev_register_recvbuf_if_possible(rhandle);
             }
        }
    }
}

/* Go thtought waiting_for_ack and rndv_inprocess list and deregister memory */
static void deregister_lists()
{
    int i;
    MPIR_RHANDLE *rhandle = NULL;
    MPIR_SHANDLE *shandle = NULL;
    vbuf *v = NULL;
    viadev_packet_rendezvous_start *packet;
    
    for (i = 0; i < viadev.np; i++) {
        viadev_connection_t *c = &viadev.connections[i];
        ack_list *wlist = &c->waiting_for_ack;
        rndv_list *rlist = &c->rndv_inprocess;

        if (i == viadev.me || 1 != c->initialized) {
            continue;
        }
        
        for(v = WAITING_LIST_GET_FIRST(wlist); 
                    v != WAITING_LIST_GET_END(wlist);
                    v = v->next) {
            viadev_packet_header *header = (viadev_packet_header*) VBUF_BUFFER_START(v);
            if (VIADEV_PACKET_RENDEZVOUS_START == VBUF_TYPE(v) && 
                VIADEV_PROTOCOL_R3 != GET_RNDV_PROTOCOL(v)) {
                packet = (viadev_packet_rendezvous_start *)VBUF_BUFFER_START(v);
                shandle = (MPIR_SHANDLE*)ID_TO_REQ(packet->sreq);
                if (NULL != shandle->dreg_entry) {
                    V_PRINT(DEBUG03, "Vbuf: De-register addr: %x key: %d\n",
                         ((struct dreg_entry*)(shandle->dreg_entry))->memhandle->addr, 
                         ((struct dreg_entry*)(shandle->dreg_entry))->memhandle->rkey);
                    dreg_decr_refcount(shandle->dreg_entry);
                }
            }
        }

        for(rhandle = WAITING_LIST_GET_FIRST(rlist); 
                    rhandle != WAITING_LIST_GET_END(rlist);
                    rhandle = rhandle->next ) {
             if (NULL != rhandle->dreg_entry) {
                 V_PRINT(DEBUG03, "Rhandle: De-register addr: %x key: %d\n",
                         ((struct dreg_entry*)(rhandle->dreg_entry))->memhandle->addr, 
                         ((struct dreg_entry*)(rhandle->dreg_entry))->memhandle->rkey);
                 dreg_decr_refcount(rhandle->dreg_entry);
             }
        }
    }
}

/* very similar to finalize */
static void stop_hca()
{
    int i;
    viadev_connection_t *c;

    NFR_PRINT("- Stoping HCA\n");
#ifdef ADAPTIVE_RDMA_FAST_PATH

    if (viadev_num_rdma_buffer) {
        NFR_PRINT("- Releaseing Eager RDMA buffers\n");
        /* unpin rdma buffers */
        for (i = 0; i < viadev.np; i++) {
            c = &viadev.connections[i];
            if (i == viadev.me) {
                continue;
            }
            if (c->RDMA_send_buf_DMA) {
                if (ibv_dereg_mr(c->RDMA_send_buf_hndl)) {
                    error_abort_all(IBV_RETURN_ERR,
                                    "could not unpin send rdma buffer");
                }
            }
            if (c->RDMA_recv_buf_DMA) {
                if (ibv_dereg_mr(c->RDMA_recv_buf_hndl)) {
                    error_abort_all(IBV_RETURN_ERR,
                                    "could not unpin recv rdma buffer");
                }
            }
        }
    }
#endif

    if (NULL == viadev.qp_hndl) {
        error_abort_all(GEN_EXIT_ERR, "Null queue pair handle");
    }

    if (viadev_use_on_demand) {
        NFR_PRINT("- Closing all active QPs\n");
        for (i = 0; i < viadev.np; i++) {
            c = &viadev.connections[i];

            if (viadev.me == i 
#ifdef _SMP_
                    || MPID_Is_local(i)
#endif
                    ) {
                continue;
            }

            if (MPICM_IB_RC_PT2PT == cm_conn_state[i]) {
                c->in_restart = 1;
                c->was_connected = 0;
                if (ibv_destroy_qp(viadev.qp_hndl[i])) {
                    error_abort_all(IBV_RETURN_ERR, "could not destroy QP");
                }
            } else {
                c->in_restart = 0;
            }
        }

        NFR_PRINT("- Closing UD CM\n");
        if (MPICM_Finalize_UD() < 0) {
            error_abort_all(IBV_RETURN_ERR, "Failed to release UD resources");
        }
    }

    /* free(viadev.qp_hndl); */

    /* The SRQ limit event thread might be in the
     * middle of posting, so wait until its done! */
    if(viadev_use_srq) {
        pthread_spin_lock(&viadev.srq_post_spin_lock);
    }

    /* Cancel thread if active */
    NFR_PRINT("- Stoping async thread\n");
    if(pthread_cancel(viadev.async_thread)) {
        error_abort_all(GEN_ASSERT_ERR,"Failed to cancel async thread\n");
    }

    pthread_join(viadev.async_thread,NULL);

    if(viadev_use_srq) {
        pthread_spin_unlock(&viadev.srq_post_spin_lock);
    }

    if(viadev_use_srq) {
        /* pthread_cond_destroy(&viadev.srq_post_cond); */
        /* pthread_mutex_destroy(&viadev.srq_post_mutex_lock);*/
        NFR_PRINT("- Destroy SRQ\n");
        if (ibv_destroy_srq(viadev.srq_hndl)) {
            error_abort_all(IBV_RETURN_ERR, "Couldn't destroy SRQ\n");
        }
    }

    {
        NFR_PRINT("- Destroy CQ\n");
        int ret = ibv_destroy_cq(viadev.cq_hndl);
        if (ret) {
            perror("ibv_destroy_cq");
            error_abort_all(IBV_RETURN_ERR,
                    "could not destroy CQ, return value=%d\n",
                    ret);
        }

    }

    if(viadev_use_blocking) {
        if (ibv_destroy_comp_channel(viadev.comp_channel)) {
            error_abort_all(IBV_RETURN_ERR, "Cannot clear completion handler");
        }
    }
    /* deregister vbufs */
    NFR_PRINT("- Deallocate VBUFs\n");
    deallocate_vbufs();

    /* unregister all user buffer registration */
    deregister_lists();
    NFR_PRINT("- Deallocate RDMA cache\n");
    while (dreg_evict());

    NFR_PRINT("- Close PD\n");
    if (ibv_dealloc_pd(viadev.ptag)) {
        error_abort_all(IBV_RETURN_ERR, "could not dealloc PD");
    }

    NFR_PRINT("- Close HCA\n");
    /* to release all resources */
    if (ibv_close_device(viadev.context)) {
        error_abort_all(IBV_RETURN_ERR, "could not close device");
    }

    viadev.context = NULL;
    NFR_PRINT("- HCA was STOPED!\n");
}

static void start_hca()
{
    int i;
    struct ibv_port_attr port_attr;
    struct timeval now, start;

    NFR_PRINT("- Starting HCA\n");

    if(ibv_query_device(viadev.context, &viadev.dev_attr)) {
        error_abort_all(GEN_EXIT_ERR,
                "Error getting HCA attributes\n");
    }   

    NFR_PRINT("- Allocating PD\n");
    viadev.ptag = ibv_alloc_pd(viadev.context);

    if(NULL == viadev.ptag) {
        error_abort_all(IBV_RETURN_ERR, "Network Recovery mechanism failed to allocate protection domain\n");
    }

    gettimeofday(&start, NULL);

    NFR_PRINT("- Reactivating port\n");
    do {
        ibv_query_port(viadev.context,
                       viadev_default_port,
                       &port_attr);
        gettimeofday(&now, NULL);
    } while (IBV_PORT_ACTIVE != port_attr.state &&
             (((now.tv_sec - start.tv_sec) * 1000000 +
               (now.tv_usec - start.tv_usec)) < nfr_timeout_on_restart * 1000000));

    if (IBV_PORT_ACTIVE != port_attr.state) {
        error_abort_all(GEN_EXIT_ERR, 
                        "Network Recovery: The port %d changed status",viadev_default_port);
    }

#ifdef TRANSPORT_RDMAOE_AVAIL
    if (IBV_LINK_LAYER_ETHERNET == port_attr.link_layer) {
        /* Enable eth over ib */
        viadev_eth_over_ib = 1;
        /* get guid */
        if (ibv_query_gid(viadev.context, viadev_default_port, 0, &viadev.my_hca_id.gid)){
            error_abort_all(GEN_EXIT_ERR, 
                    "Failed to query MAC on port %d",viadev_default_port);
        }
    } else
#else
        if (viadev_eth_over_ib) {
            /* get guid */
            if (ibv_query_gid(viadev.context, viadev_default_port, 0, &viadev.my_hca_id.gid)){
                error_abort_all(GEN_EXIT_ERR, 
                        "Failed to query MAC on port %d",viadev_default_port);
            }
        } else
#endif
        {
            /* Disable eth over ib */
            viadev.my_hca_id.lid = port_attr.lid;
        }
    /* update lids */
    viadev.lgid_table[viadev.me] = viadev.my_hca_id;
    viadev.port_attr = port_attr;
    viadev.lmc = port_attr.lmc;

    NFR_PRINT("- Create CQ\n");
    viadev.cq_hndl = create_cq(NULL);

    NFR_PRINT("- Re-register VBUFs\n");
    re_register_vbufs();

    /* Re-Register all vbufs that were registered till now */
    NFR_PRINT("- Create SRQ\n");
    if(viadev_use_srq) {
        viadev.srq_hndl = create_srq();

        viadev.srq_zero_post_counter = 0;
        pthread_spin_lock(&viadev.srq_post_spin_lock);
        viadev.posted_bufs = viadev_post_srq_buffers(viadev_srq_fill_size);

        {
            struct ibv_srq_attr srq_attr;
            srq_attr.max_wr = viadev_srq_alloc_size;
            srq_attr.max_sge = 1;
            srq_attr.srq_limit = viadev_srq_limit;

            if (ibv_modify_srq(viadev.srq_hndl, &srq_attr, IBV_SRQ_LIMIT)) {
                error_abort_all(IBV_RETURN_ERR, "Couldn't modify SRQ limit\n");
            }
        }

        pthread_spin_unlock(&viadev.srq_post_spin_lock);
    }

    /* Start the async thread which watches
     * for SRQ limit and other aysnchronous events */

    NFR_PRINT("- Create async thread\n");
    pthread_create(&viadev.async_thread, NULL,
	    (void *) async_thread, (void *) viadev.context);

#ifdef ADAPTIVE_RDMA_FAST_PATH
    /* Restart Eager RDMA buffers */
    for (i = 0; i < viadev.np; i++) {
            viadev_connection_t *c = &viadev.connections[i];

            if (c->RDMA_send_buf_DMA) {
                c->RDMA_send_buf_hndl = register_memory(c->RDMA_send_buf_DMA, 
                                             viadev_vbuf_total_size * viadev_num_rdma_buffer);
            }

            if (c->RDMA_recv_buf_DMA) {
                c->RDMA_recv_buf_hndl = register_memory(c->RDMA_recv_buf_DMA, 
                                             viadev_vbuf_total_size * viadev_num_rdma_buffer);
            }
    }
#endif
    /* Reset init attributes */
    memset(&cm_ib_qp_attr.rc_qp_init_attr, 0, sizeof(struct ibv_qp_init_attr));
    cm_ib_qp_attr.rc_qp_init_attr.send_cq = viadev.cq_hndl;
    cm_ib_qp_attr.rc_qp_init_attr.recv_cq = viadev.cq_hndl;
    if(viadev_use_srq) {
        cm_ib_qp_attr.rc_qp_init_attr.srq = viadev.srq_hndl;
        cm_ib_qp_attr.rc_qp_init_attr.cap.max_recv_wr = 0;
    } else {
        cm_ib_qp_attr.rc_qp_init_attr.srq = NULL;
        cm_ib_qp_attr.rc_qp_init_attr.cap.max_recv_wr = viadev_rq_size;
    }

    cm_ib_qp_attr.rc_qp_init_attr.cap.max_send_wr = viadev_sq_size;
    cm_ib_qp_attr.rc_qp_init_attr.cap.max_send_sge = viadev_default_max_sg_list;
    cm_ib_qp_attr.rc_qp_init_attr.cap.max_recv_sge = 1;
    cm_ib_qp_attr.rc_qp_init_attr.cap.max_inline_data = viadev_max_inline_size;
    cm_ib_qp_attr.rc_qp_init_attr.qp_type = IBV_QPT_RC;

    NFR_PRINT("- Restart UD CM\n");
    
    MPICM_Init_lock();
    /* Restart UDCM */
    if(MPICM_Init_UD(&(viadev.ud_qpn_table[viadev.me]))) {
        error_abort_all(GEN_EXIT_ERR, "MPICM_Init_UD");
    }
    if (MPICM_Connect_UD(viadev.ud_qpn_table, viadev.lgid_table)) {
        error_abort_all(GEN_EXIT_ERR, "MPICM_Connect_UD");
    }

    NFR_PRINT("- Reregister memory\n");
    reregister_lists();
}

/*********************************************************************/
/*********************************************************************/

char* type2name(int type)
{
    switch(type) {
        case FAST_EAGER_CACHED:
            return "FAST_EAGER_CACHED";
        case VIADEV_PACKET_EAGER_START:
            return "VIADEV_PACKET_EAGER_START";
        case VIADEV_PACKET_EAGER_NEXT:
            return "VIADEV_PACKET_EAGER_NEXT";
        case VIADEV_PACKET_RENDEZVOUS_START:
            return "VIADEV_PACKET_RENDEZVOUS_START";
        case VIADEV_PACKET_RENDEZVOUS_START_RETRY:
            return "VIADEV_PACKET_RENDEZVOUS_START_RETRY";
        case VIADEV_PACKET_RENDEZVOUS_REPLY:
            return "VIADEV_PACKET_RENDEZVOUS_REPLY";
        case VIADEV_PACKET_R3_DATA:
            return "VIADEV_PACKET_R3_DATA";
        case VIADEV_PACKET_R3_ACK:
            return "VIADEV_PACKET_R3_ACK";
        case VIADEV_PACKET_RPUT_FINISH:
            return "VIADEV_PACKET_R3_ACK";
        case VIADEV_PACKET_RGET_FINISH:
            return "VIADEV_PACKET_RGET_FINISH";
        case VIADEV_RDMA_ADDRESS:
            return "VIADEV_RDMA_ADDRESS";
        case VIADEV_PACKET_BARRIER:
            return "VIADEV_PACKET_BARRIER";
        case VIADEV_PACKET_NOOP:
            return "VIADEV_PACKET_NOOP";
        case VIADEV_PACKET_EAGER_COALESCE:
            return "VIADEV_PACKET_EAGER_COALESCE";
#ifdef MCST_SUPPORT
        case VIADEV_PACKET_RENDEZVOUS_CANCEL:
            return "VIADEV_PACKET_RENDEZVOUS_CANCEL";
        case VIADEV_PACKET_UD_MCST:
            return "VIADEV_PACKET_UD_MCST";
#endif
#ifdef VIADEV_SEND_CANCEL
        case VIADEV_SEND_CANCEL_REQUEST:
            return "VIADEV_SEND_CANCEL_REQUEST";
        case VIADEV_SEND_CANCEL_REPLY:
            return "VIADEV_SEND_CANCEL_REPLY";
        case VIADEV_SEND_CANCEL_ACK:
            return "VIADEV_SEND_CANCEL_ACK";
#endif

#ifdef MEMORY_RELIABLE
        case VIADEV_PACKET_CRC_ACK:
            return "VIADEV_PACKET_CRC_ACK";
#endif
        default:
            return "UNKNOW";
    }
}

char* padding2name(int name)
{
    switch(name) {
        case NORMAL_VBUF_FLAG:
            return "NORMAL_VBUF_FLAG";
        case RPUT_VBUF_FLAG:
            return "RPUT_VBUF_FLAG";
        case RGET_VBUF_FLAG:
            return "RGET_VBUF_FLAG";
        case FREE_FLAG:
            return "FREE_FLAG";
        case BUSY_FLAG:
            return "BUSY_FLAG";
        default:
            return "UNKNOW";
    }
}

/* Scan all connections and print connections with pending messages and */
void nfr_finalize()
{
    int i;
    for (i = 0; i < viadev.np; i++)
    {
        if (viadev.me == i) {
            if (WAITING_LIST_LEN((&viadev.connections[i].waiting_for_ack)))
                NFR_PRINT("NFR: Error, Connection %d waiting list len [%d]\n",
                        i, WAITING_LIST_LEN((&viadev.connections[i].waiting_for_ack)));
            if (viadev.connections[i].pending_acks)
                NFR_PRINT("NFR: Error, Connection %d found [%d] pending acks\n",
                        i, viadev.connections[i].waiting_for_ack);
        }
    }
}

void nfr_init()
{
    char *value;
    if (viadev_use_nfr) {

        nfr_fatal_error = NO_FATAL;

        if (VIADEV_PROTOCOL_RGET != viadev_rndv_protocol &&
                VIADEV_PROTOCOL_R3 != viadev_rndv_protocol) {
            if (0 == viadev.me)
                NFR_PRINT("WARNING: The NFR feature supports only RGET and R3 protocols, fallback to RGET[%d]\n"
                        , viadev_rndv_protocol);
            viadev_rndv_protocol = VIADEV_PROTOCOL_RGET;
        }
        /* Make sure that we enable on_demand connections in NFR mode */
        viadev_on_demand_threshold = 0;
        viadev_use_on_demand = 1;
        /* will be used in feature */
        if ((value = getenv("VIADEV_NFR_MAX_FAILURES")) != NULL) {
            nfr_max_failures = atoi(value);
            if (nfr_max_failures < 0)
                nfr_max_failures = NFR_MAX_FAILURES;
        }
        if ((value = getenv("VIADEV_NFR_TIMEOUT_ON_ERROR")) != NULL) {
            nfr_timeout_on_error = atoi(value);
            if (nfr_timeout_on_error < 0)
                nfr_timeout_on_error = NFR_DEFAULT_TIMEOUT_ON_ERROR;
        }
    }
}

void nfr_process_retransmit (viadev_connection_t *c, viadev_packet_header *header)
{
    viadev_packet_header *lh;
    viadev_packet_rendezvous_start* h = 
        (viadev_packet_rendezvous_start*)header;
    struct rndv_list *l = &(c->rndv_inprocess);
    MPIR_RHANDLE *rhandle = NULL;

    /* we are in debug mode got old messages */
    V_PRINT(DEBUG03, "We are in nfr recovery mode[%d][%s][%d]\n", c->progress_recov_mode,
            type2name(header->type), header->id);
    /* Is it in unexpected list  ? */
    if ( MPI_SUCCESS == 
            MPID_nfr_search_rndv_start(h->envelope.src_lrank, h->envelope.tag, 
                                      h->envelope.context, h->buffer_address, 
                                      header->id, h->memhandle_rkey)) {
        /* the randevouze request is still waiting on unexpected list */
        V_PRINT(DEBUG03, "NFR: Message was found in Unexpected list\n");
        return;
    }
    /* Is it in rdma failed list ? */
    for( rhandle = WAITING_LIST_GET_FIRST(l); 
            rhandle != WAITING_LIST_GET_END(l);
            rhandle = rhandle->next ) {
        if (rhandle->sn == header->id && 
                rhandle->remote_address == h->buffer_address) {
            /* we found our packet ! */
            rhandle->was_retransmitted = 1;

            if (NULL == rhandle->fin) {
                /* it did not finish RDMA read stage */
                rhandle->remote_memhandle_rkey = h->memhandle_rkey;
                viadev_recv_rget(rhandle);
                V_PRINT(DEBUG03, "NFR: Message was found in rndv_inprocess list, the READ was not finished\n");
                return;
            } else {
                /* the message was send but we did not got completion
                 * on fin message ... 
                 * Release the old VBUF and resend FIN message */
                vbuf *v_fin = rhandle->fin;
#ifdef ADAPTIVE_RDMA_FAST_PATH
                if (v_fin->padding == NORMAL_VBUF_FLAG) {
                    release_vbuf(v_fin);
                } else {
                    v_fin->padding = FREE_FLAG;
                    v_fin->len = 0;
                }
#else
                release_vbuf(v);
#endif
                V_PRINT(DEBUG03, "NFR: Message was found in rndv_inprocess list, the FIN was not completed\n");
                viadev_rget_finish(rhandle);

                return;
            }
        }
    }

    /* If we here  the rdma read was done but the fin message was not sent from 
     * some reason. So lets just send the fin message */

    /* allocate dummy recive handle and retrasmit fin message */
    MPID_RecvAlloc(rhandle);
    /* fill only required data */
    rhandle->connection = c;
    rhandle->send_id = 
        ((viadev_packet_rendezvous_start *)header)->sreq;

    /* When we will get complition on this fin send message the remote_address = 0 will 
     * help to us to understand that this one was dummy rhandle and we should just release it */
    rhandle->remote_address = 0; 
    /* send fin message */
    viadev_rget_finish(rhandle);
    /* release dummy rhandle */
    /* MPID_RecvFree(rhandle); we will release it later */
    V_PRINT(DEBUG03, "NFR: Message was completed, resending fin %d\n", l->size);
    return;

}

void nfr_incoming_nfr_req (viadev_packet_header *h, viadev_connection_t *c)
{
    int peer = c->global_rank;
    V_PRINT(DEBUG03, "NFR: Got reconnect request from [%d]\n", peer);

    /* Update connection status */
    MPICM_Server_connection_establish(peer); 
    /* Clean connection stuff */
    odu_re_enable_qp(peer);
    /* Update reciver that connection was restore,
       so it will exit fron poll loops*/
    if(QP_REC == c->qp_status) {
        V_PRINT(DEBUG03, "NFR: decrease nfr_num_of_bad_connections st %d num %d\n",
                c->qp_status, nfr_num_of_bad_connections-1);
        nfr_num_of_bad_connections--;  
        c->qp_status = QP_UP;
    }

    /* Switch connection status back to connected state */
    V_PRINT(DEBUG03, "NFR: connection was was in [%d] setting it to PT2PT\n", 
            cm_conn_state[peer]);
    assert (c->vi == viadev.qp_hndl[peer]);
    /* switch progress engine to debug mode */
    c->progress_recov_mode = 1;
    /* Syn eager rdma tails */
    c->ptail_RDMA_send = h->rdma_credit;
    /* Send replay with last recved packet */
    send_reconnect_rep(&viadev.connections[peer]);
    /* Re-Send all relevant data to the remote side */
    send_lost_data(c, h->ack);
    /* if we had pendings */
    if (viadev.pending_req_head[peer]) {
        cm_process_queue(peer);
    }

    V_PRINT(DEBUG03, "NFR: Leaving nfr_incoming_nfr_req\n");
}

void nfr_incoming_nfr_rep (viadev_packet_header *h, viadev_connection_t *c)
{
    int peer = c->global_rank;
    V_PRINT(DEBUG03, "NFR: Got reconnect reply from [%d]\n", peer);
    
    /* Put the progress engeene in network recovery mode */
    c->progress_recov_mode = 1;
    /* Syn eager rdma tails */
    c->ptail_RDMA_send = h->rdma_credit;
    /* Re-Send all relevant data to the remote side */
    send_lost_data(c, h->ack);

    MPICM_Lock();
    /* Switch connection status back to connected state */
    cm_conn_state[peer] = MPICM_IB_RC_PT2PT;
    nfr_wait_for_replies--;
    MPICM_Unlock();
}

void nfr_incoming_nfr_fin (viadev_connection_t *c)
{
    /* It is posssible that sender got completion on some rndv messages and
     * recv doesnot got. Such messages will not be re-transmited because
     * they were remove on sender side from waiting list. So if we have some 
     * rndv in process that were not completed and they sent fin message we way
     * mark them as completed */
    struct rndv_list *l = &(c->rndv_inprocess);
    MPIR_RHANDLE *rhandle = NULL, *r_next = NULL;
    vbuf *v = NULL;

    r_next = WAITING_LIST_GET_FIRST(l);
    while (r_next != WAITING_LIST_GET_END(l)) {  /* I can not user FOR becasue rhandle may be released */
        rhandle = r_next;
        r_next = r_next->next;
        if (rhandle->was_retransmitted) {
            /* Nothing to do, just ignore */
            V_PRINT(DEBUG03, "fin, rndv %d was retransmitted\n", rhandle->sn);
            continue;
        }

        assert(NULL != rhandle->fin);
        v = rhandle->fin;
        NFR_REMOVE_FROM_WAITING_LIST(l, rhandle);
        RECV_COMPLETE(rhandle);
        V_PRINT(DEBUG03,"Droping rhandle sn %d\n", rhandle->sn);
#ifdef ADAPTIVE_RDMA_FAST_PATH
        if (v->padding == NORMAL_VBUF_FLAG) {
            release_vbuf(v);
        } else {
            v->padding = FREE_FLAG;
            v->len = 0;
        }
#else
        release_vbuf(v);
#endif
    }
}

static int pool_for_recovery()
{
    struct timeval now, start;
    viadev_connection_t *c = NULL;
    int i;

    gettimeofday(&start, NULL);
    gettimeofday(&now, NULL);

    while ((((now.tv_sec - start.tv_sec) * 1000000 +  
                    (now.tv_usec - start.tv_usec)) < nfr_timeout_on_error * 1000000) && /* timeout expired */
            nfr_num_of_bad_connections) { 
        /* Progress for new packets*/
        if (poll4data()) {
            gettimeofday(&start, NULL); /* If we got new error we are restarting timer */
        }
        /* update timer */
        gettimeofday(&now, NULL);
    }

    if (nfr_num_of_bad_connections > 0) {
        for (i = 0; i < viadev.np; i++) {
            c = &viadev.connections[i];
            if (QP_DOWN == c->qp_status) {
                /* Send CM connect request here */
                NFR_PRINT("Failed restore connection to rank %d\n",i);
            }
        }
        return -1;
    }
    /* Send RC reconnect request */
    /*****************************/
    nfr_wait_for_replies = 0;
    for (i = 0; i < viadev.np; i++) {
        c = &viadev.connections[i];
        if (QP_REC_D == c->qp_status) {
            V_PRINT(DEBUG03, "Sending reconnect request to rank %d\n",i);
            /* Reset MPI level connection to disconnect 
             * We will switch it back to connected after all retrassmitions */
            cm_conn_state[i] = MPICM_IB_NONE;
            c->qp_status = QP_UP;
            /* Make prepost */
            /* On reply on this connect we will switch the connection state
             * back to PT2PT connected state */
            send_reconnect_req(c);
            nfr_wait_for_replies++;
        }
    }
    /* We must wait for all replies. If we will not wait Irecv may happend , it may cause 
     * ibv_post send which will be added to waiting_for_ack list and after reply will be retransmitted.
     * So as result remote side will get double packet.
     */
    while(nfr_wait_for_replies > 0) {
        MPID_DeviceCheck(MPID_NOTBLOCKING);
    }

    return 0;
}
/* The heart of the network recovery */
int nfr_process_qp_error(struct ibv_wc *comp)
{
    if (NULL != comp) {
        process_bad_completion(comp); /* some old qp flush error, just ignore */
    }

    if (pool_for_recovery() < 0) {
        NFR_PRINT("<---- Failed to recover ---->\n");
        return -1;
    }

    NFR_PRINT("<---- All bad connections were restored ---->\n");
    return 0;
}

/* Prepare recv qp after NFR restart */
void nfr_prepare_qp(int peer)
{
    /* We post single recv to the qp, it will be used for RC reconnect message */
    if(!viadev_use_srq)
        DIRECT_POST_VBUF_RECV(peer);
}

/* This functions is responsible for HCA restart */
int nfr_restart_hca ()
{
    struct timeval now, start;
    int i;

    NFR_PRINT(" > Handling fatal event on the HCA:\n");
    /* Release all hca resources close the device */
    stop_hca();
    sleep(10);
    /* Try to reopen the hca */
    gettimeofday(&start, NULL);
    gettimeofday(&now, NULL);

    viadev.context = NULL;

    while ((((now.tv_sec - start.tv_sec) * 1000000 +  
            (now.tv_usec - start.tv_usec)) < nfr_timeout_on_restart * 1000000) && /* timeout expired */
            NULL == viadev.context) { 
        /* Progress for new packets*/
        viadev.context = ibv_open_device(viadev.nic);
        /* update timer */
        gettimeofday(&now, NULL);
    }

    if (NULL == viadev.context) {
        error_abort_all(IBV_RETURN_ERR, "Network Recovery mechanism failed to reopen HCA. Exiting...\n");
    }
    /* Ok, we were able to reopen hca !!! Now we should reallocate all other resources */
    NFR_PRINT(" > HCA is back:\n");
    start_hca();

    nfr_fatal_error = IN_FATAL;

    /* Start reconnect for connections that were connected */
    NFR_PRINT(" > Sending reconect requests\n");

    nfr_num_of_bad_connections = 0;

    for (i = 0; i < viadev.np; i++) {
        viadev_connection_t *c = &viadev.connections[i];
        if (viadev.me == i 
#ifdef _SMP_
                || MPID_Is_local(i)
#endif
                ) {
            continue;
        }
        V_PRINT(DEBUG03, "Was connected %d %d\n", i, c->in_restart);
        if (1 == c->in_restart) {
            V_PRINT(DEBUG03, "Sending reconnect to %d\n", c->global_rank);
            reconnect(c);
        }
    }

    /* Wait for full restore */
    if (pool_for_recovery() < 0) {
        NFR_PRINT("<---- Failed to recover ---->\n");
        return -1;
    }

    NFR_PRINT(" > Recovery done\n");
    /* disable fatal */
    
    return 0;
}
