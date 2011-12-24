/*
 * Copyright (C) 1999-2001 The Regents of the University of California
 * (through E.O. Lawrence Berkeley National Laboratory), subject to
 * approval by the U.S. Department of Energy.
 *
 * Use of this software is under license. The license agreement is included
 * in the file MVICH_LICENSE.TXT.
 *
 * Developed at Berkeley Lab as part of MVICH.
 *
 * Authors: Bill Saphir      <wcsaphir@lbl.gov>
 *          Michael Welcome  <mlwelcome@lbl.gov>
 */

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
#include "ibverbs_header.h"
#include "viutil.h"
#include "nfr.h"
#include "viapriv.h"
#include "viapacket.h"
#include "queue.h"
#include "viaparam.h"
#include "mpid_smpi.h"
#include "vbuf.h"

#include "cm.h"
#include "cm_user.h"

#ifdef MCST_SUPPORT
extern int ud_prepost_count;
extern int ud_prepost_threshold;
extern int period;
extern int retransmission;
void viadev_process_msend(void *);
void viadev_process_mrecv(void *);
void viadev_rendezvous_cancel(MPIR_RHANDLE *);;
void process_acks(void);
void slide_window(void);
void check_time_out(void);
void viadev_post_ud_recv(vbuf *);
extern int is_mcast_enabled;
#endif

int perform_manual_apm(struct ibv_qp*);
int reload_alternate_path(struct ibv_qp *);
static void lock_apm();
static void viutil_spinandwaitcq(struct ibv_wc *);
/*
 * MPID_DeviceCheck processes ALL completed descriptors. 
 * This means it handles ALL incoming packets as well (as 
 * takes care of marking send requests complete when data is
 * transferred -- still not sure if it actually does something here 
 * 
 * Each time it is
 * called, it processes ALL descriptors that have completed since
 * the last time it was called, even if it was called nonblocking. 
 * 
 * If the blocking form is called, and there are no packets waiting, 
 * it spins for a short time and then waits on the connection queue 
 * 
 * Most of the work of the VIA device is done in this routine. 
 *
 * As this routine is on the critical path, will have to work hard
 * to optimize it. Reducing function calls will be important. 
 */


/* 
 * The flowlist is a list of connections that have received new flow 
 * credits and may be able to make progress on pending rendezvous sends.
 * The flowlist is a singly-linked list of connections, linked by 
 * the "nextflow" field in the connection. In order to prevent loops, 
 * there is also a connection field "inflow" which is set to one
 * if it is in the flowlist and zero otherwise. It must be 
 * initialized to 0 and it only changed by the flow control 
 * logic in MPID_CheckDevice
 * 
 * We add a connection to the flowlist when either
 *  1) it gets new credits and there is a rendezvous in progress
 *  2) a rendezvous ack is received
 * 
 * Note: flowlist fields are touched only routines in this file. 
 */

static pthread_spinlock_t apm_lock;

void init_apm_lock()
{
    pthread_spin_init(&apm_lock, 0);
}

static void lock_apm()
{
    pthread_spin_lock(&apm_lock);
    return;
}

static void unlock_apm()
{
    pthread_spin_unlock(&apm_lock);
    return;
}


static viadev_connection_t *flowlist;
#define PUSH_FLOWLIST(c) {                                          \
    if (0 == c->inflow) {                                           \
        c->inflow = 1;                                              \
        c->nextflow = flowlist;                                     \
        flowlist = c;                                               \
    }                                                               \
}

#define POP_FLOWLIST() {                                            \
    if (flowlist != NULL) {                                         \
        viadev_connection_t *_c;                                    \
        _c = flowlist;                                              \
        flowlist = _c->nextflow;                                    \
        _c->inflow = 0;                                             \
        _c->nextflow = NULL;                                        \
    }                                                               \
}

#if defined(_IA64_)
   #if defined(__INTEL_COMPILER)
      #define RMB()  __mf();
   #else
      #define RMB()  asm volatile ("mf" ::: "memory");
   #endif
#elif defined(_IA32_)
      #define RMB()  asm volatile ("lfence" ::: "memory");
#elif defined(_X86_64_)
      #define RMB()  asm volatile ("lfence" ::: "memory");
#elif defined(_EM64T_)
      #define RMB()  asm volatile ("lfence" ::: "memory");
#elif defined(_PPC64_)
      #define RMB()  asm volatile ("sync": : :"memory");
#else
      #define RMB()
#endif                          /* defined(_IA64_ ... */

static void process_flowlist(void);

/*
 * Attached to each connection is a list of send handles that
 * represent rendezvous sends that have been started and acked but not
 * finished. When the ack is received, the send is placed on the list;
 * when the send is complete, it is removed from the list.  The list
 * is an "in progress sends" queue for this connection.  We need it to
 * remember what sends are pending and to remember the order sends
 * were acked so that we complete them in that order. This
 * prevents a situation where we receive an ack for message 1, block
 * because of flow control, receive an ack for message 2, and are able
 * to complete message 2 based on the new piggybacked credits.
 * 
 * The list head and tail are given by the shandle_head and
 * shandle_tail entries on viadev_connection_t and the list is linked
 * through the nexthandle entry on a send handle.  
 *
 * The queue is FIFO because we must preserve order, so we maintain
 * both a head and a tail. 
 *
 */

#define RENDEZVOUS_IN_PROGRESS(c, s) {                              \
    if (NULL == c->shandle_tail) {                                  \
        c->shandle_head = s;                                        \
    } else {                                                        \
        c->shandle_tail->nexthandle = s;                            \
    }                                                               \
    c->shandle_tail = s;                                            \
    s->nexthandle = NULL;                                           \
}

#define RENDEZVOUS_DONE(c) {                                        \
    c->shandle_head = c->shandle_head->nexthandle;                  \
        if (NULL == c->shandle_head) {                              \
            c->shandle_tail = NULL;                                 \
        }                                                           \
}

#ifdef ADAPTIVE_RDMA_FAST_PATH

int poll_rdma_buffer(void **vbuf_addr, int *out_of_order)
{
    int i, j;
    static int last_polled = 0;
    int max_conn;
    vbuf *v;
    viadev_packet_header *h;
    VBUF_FLAG_TYPE size;
    volatile VBUF_FLAG_TYPE *tail;
    volatile VBUF_FLAG_TYPE *head;
    viadev_connection_t *c = NULL;

    *vbuf_addr = NULL;
    *out_of_order = 0;

    if (viadev_num_rdma_buffer == 0)
        return 1;

    max_conn = viadev.RDMA_polling_group_size;
    if (VIADEV_UNLIKELY(0 == max_conn))
        return 1;

    for (i = last_polled, j = 0; 
            j < max_conn;
            i = (i + 1) % max_conn, j++) {

        c = viadev.RDMA_polling_group[i];

        /* get the current polling buffer */
        v = &(c->RDMA_recv_buf[c->p_RDMA_recv]);
        head = v->head_flag;
        /* check the flag */
        if (*head) {

            size = (*head & FAST_RDMA_SIZE_MASK);
            tail = (VBUF_FLAG_TYPE *) ((aint_t) (v->buffer) + size);

            /* check tail */

            /* If tail has not come go ahead and poll other
             * connections. Busy waiting for a message being
             * DMA'd to user memory causes unnecessary loss of
             * CPU cycles */
            if (VIADEV_LIKELY(*tail == *head)) {

                /* Read memory barrier - make sure the header is read
                 * only after the head and tails are read and compared
                 * disabled by default for PGI Compiler*/
#ifndef DISABLE_RMB
                RMB();
#endif
                /* out of order message? */
                h = (viadev_packet_header *) (v->buffer);

                if (VIADEV_LIKELY(h->id == c->next_packet_expected ||
                                  h->type == VIADEV_PACKET_NOOP)) {
                    *vbuf_addr = v;
                    *out_of_order = 0;

                    /* clear head flag */
                    *head = 0;

                    /* advance receive pointer */
                    if (++(c->p_RDMA_recv) >= viadev_num_rdma_buffer)
                        c->p_RDMA_recv = 0;

                    last_polled = i;
                    return 0;
                }
            }
        }
    }

    return 1;
}

#endif                          /* ADAPTIVE_RDMA_FAST_PATH */


int MPID_DeviceCheck(MPID_BLOCKING_TYPE blocking)
{
    int ret1, ret2, ne;

#ifdef MEMORY_RELIABLE
    int ret5;
#endif
    struct ibv_wc rc, sc;
    int gotone = 0;
    void *vbuf_addr;

#ifdef ADAPTIVE_RDMA_FAST_PATH
    int ofo;
#endif

#ifdef MCST_SUPPORT
    int ret3 = 1;
#endif

#ifdef MEMORY_RELIABLE
    vbuf* v;
    viadev_packet_header *h;
    viadev_connection_t *c;
#endif
    D_PRINT("Enter DeviceCheck Blocking = %s",
            (blocking == MPID_NOTBLOCKING ? "FALSE" : "TRUE"));

  checkstart:
    flowlist = NULL;

    /* process all waiting packets */
    ret1 = ret2 = 0;

  
    /* Check the fastest channel */
#ifdef _SMP_
    if (!disable_shared_mem && SMP_INIT) {
        if (MPI_SUCCESS == MPID_SMP_Check_incoming())
            gotone = 1;

    }
#endif

#ifdef MCST_SUPPORT
    if ((viadev.bcast_info.bcast_called == 1) && (is_mcast_enabled)) {

        if (retransmission == 0) {

            /* Calling this if there are no retransmissions else where */
            process_acks();

            slide_window();

            check_time_out();

        }
    }
#endif

#ifdef MCST_SUPPORT
    while ((ret1 == 0) ||
           ((ret2 > 0) && (viadev.bcast_info.bcast_called == 1)
            && (is_mcast_enabled)) ||
           ((ret3 > 0) && (viadev.bcast_info.bcast_called == 1)
            && (is_mcast_enabled))) {
#else
    while (ret1 == 0) {         /* at least success on one */
#endif
        /* Chek if it was fatal */
        if (VIADEV_UNLIKELY(1 == viadev_use_nfr && FATAL == nfr_fatal_error)) {
            if (nfr_restart_hca() < 0) {
                int dest_rank = ((vbuf *) vbuf_addr)->grank;
                error_abort_all(GEN_EXIT_ERR,
                                "Failed restore connection to [%d:%s]",
                                dest_rank, viadev.processes[dest_rank]);
            }
            return MPI_SUCCESS;
        }
#ifdef MCST_SUPPORT
        if ((viadev.bcast_info.bcast_called == 1) && (is_mcast_enabled)) {

            ret2 = ibv_poll_cq(viadev.ud_scq_hndl, 1, &sc);
            if (ret2 == 1) {
                vbuf_addr = (void *) ((aint_t) sc.wr_id);
                if (sc.status != IBV_WC_SUCCESS) {
                    int dest_rank = ((vbuf *) vbuf_addr)->grank;
                    error_abort_all(IBV_RETURN_ERR,
                                    "Got completion with error %s, "
                                    "code=%d, dest [%d:%s]",
                                    wc_code_to_str(sc.status), sc.status,
                                    dest_rank, viadev.processes[dest_rank]);

                }
                viadev_process_msend(vbuf_addr);
            }

            ret3 = ibv_poll_cq(viadev.ud_rcq_hndl, 1, &sc);
            if (ret3 == 1) {
                vbuf_addr = (void *) ((aint_t) sc.wr_id);
                if (sc.status != IBV_WC_SUCCESS) {
                    int dest_rank = ((vbuf *) vbuf_addr)->grank;
                    error_abort_all(IBV_RETURN_ERR,
                                    "Got completion with error %s, "
                                    "code=%d, dest [%d:%s]",
                                    viadev.processes[viadev.me], viadev.me,
                                    wc_code_to_str(sc.status), sc.status,
                                    dest_rank, viadev.processes[dest_rank]);
                }
                co_print("ud_pkt recvd \n");
                viadev_process_mrecv(vbuf_addr);
            }
        }
#endif

        /* Check for new connections*/
        odu_test_new_connection();
                
#ifdef ADAPTIVE_RDMA_FAST_PATH
        ret1 = poll_rdma_buffer(&vbuf_addr, &ofo);
        if (ret1 == 0) {
            gotone = 1;
            viadev_process_recv(vbuf_addr);
        }
#endif

        ne = ibv_poll_cq(viadev.cq_hndl, 1, &sc);
        if (ne < 0) {
            error_abort_all(IBV_RETURN_ERR,
                            "Error polling CQ (ibv_poll_cq() returned %d)",
                            ne);
        } else if (ne > 1) {
            error_abort_all(IBV_RETURN_ERR,
                            "Asked only one completion, "
                            "got more (ibv_poll_cq() returned %d)",
                            ne);
        } else if (1 == ne) {
#ifdef MCST_SUPPORT
            if (sc.wr_id == -1) {
                viadev.bcast_info.sbuf_tail =
                    (++viadev.bcast_info.sbuf_tail) % MAX_ACKS;
                viadev.bcast_info.ack_full = 0;
            } else {
#endif

                gotone = 1;
                vbuf_addr = (void *) ((aint_t) sc.wr_id);
                ret1 = 0;

                /* Need to check if it is a completion with error */
                if (sc.status != IBV_WC_SUCCESS) {
                    int dest_rank = ((vbuf *) vbuf_addr)->grank;
                    if (!viadev_use_nfr) {
                        error_abort_all(IBV_RETURN_ERR,
                                "Got completion with error %s, "
                                "code=%d, dest [%d:%s]",
                                wc_code_to_str(sc.status), sc.status,
                                dest_rank, viadev.processes[dest_rank]);
                    } else {
                        NFR_PRINT("[%d:%s] Got completion with error %s, "
                                "code=%d, dest [%d:%s], qp_num %x - Starting Network Recovery process",
                                viadev.me, viadev.processes[viadev.me],
                                wc_code_to_str(sc.status), sc.status,
                                dest_rank, viadev.processes[dest_rank],
                                sc.qp_num);
                        if (nfr_process_qp_error(&sc) < 0) {
                            error_abort_all(GEN_EXIT_ERR,
                                            "Failed restore connection to [%d:%s]",
                                            dest_rank, viadev.processes[dest_rank]);
                        }
                        return MPI_SUCCESS;
                    }
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
#ifdef MEMORY_RELIABLE

                    v = (vbuf *) vbuf_addr;
                    h = (viadev_packet_header *) (VBUF_BUFFER_START(v));
                    c = &(viadev.connections[h->src_rank]);
                    if (h->id == c->next_packet_expected  ||
                            h->type == VIADEV_PACKET_NOOP) {
                        viadev_process_recv(vbuf_addr);
                    }
                    else {
                        ret1 = 1;
                        enqueue_ofo_queue(vbuf_addr, c);
                    }
#else
                    viadev_process_recv(vbuf_addr);
#endif
                }

#ifdef MCST_SUPPORT
            }
#endif
        } else {
            ret1 = 1;
        }
#ifdef MEMORY_RELIABLE
        do{
            ret5 = check_ofo_queues(&vbuf_addr);
            if (ret5 == 0) {
                viadev_process_recv(vbuf_addr);
            }
        }while(0 == ret5);
#endif

    }

    /* make progress on pending rendezvous transfers */
    if (flowlist) {
        process_flowlist();
        /* In case some new credits have come in while processing
         * go check again. Little overhead to do this.
         */
        goto checkstart;
    }

    if (gotone || blocking == MPID_NOTBLOCKING) {
        D_PRINT("Leaving DeviceCheck gotone = %d", gotone);
        return gotone ? MPI_SUCCESS : -1;       /* nonblock must return -1 */
    }

    rc.wr_id = 0;               /* for sanity */
    sc.wr_id = 0;
    viutil_spinandwaitcq(&sc);

#ifdef MCST_SUPPORT
    if (sc.wr_id == -1) {
        viadev.bcast_info.sbuf_tail =
            (++viadev.bcast_info.sbuf_tail) % MAX_ACKS;
        viadev.bcast_info.ack_full = 0;
    } else {
#endif

        if (sc.wr_id) {         /* both in sc */
            vbuf_addr = (void *) ((aint_t) sc.wr_id);
            if (viadev_use_on_demand && sc.opcode == IBV_WC_RECV) {
                int rank;
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
                    /*First packet packet from this peer*/
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
#ifdef MEMORY_RELIABLE
                v = (vbuf *) vbuf_addr;
                h = (viadev_packet_header *) (VBUF_BUFFER_START(v));
                c = &(viadev.connections[h->src_rank]);
                if (h->id == c->next_packet_expected  ||
                        h->type == VIADEV_PACKET_NOOP) {
                    viadev_process_recv(vbuf_addr);
                } else {
                    ret1 = 1;
                    enqueue_ofo_queue(vbuf_addr, c);
                }
#else
                viadev_process_recv(vbuf_addr);
#endif
            }
        }
#ifdef MCST_SUPPORT
    }
#endif

    if (flowlist) {
        process_flowlist();
    }
    D_PRINT("Leaving DeviceCheck after wait");

    return MPI_SUCCESS;
}

#ifdef MCST_SUPPORT

void viadev_process_msend(void *vbuf_addr)
{
    vbuf *v;
    v = (vbuf *) (vbuf_addr);
    release_vbuf(v);
}

void viadev_process_mrecv(void *vbuf_addr)
{
    vbuf *v = NULL;
    viadev_packet_eager_start *header = NULL;
    MPIR_RHANDLE *rhandle;
    int Bcnt, root, found;

    co_print("Entering process mrecv\n");

    --ud_prepost_count;
    co_print("ud_prepost_count:%d, ud_prepost_threshold:%d\n",
             ud_prepost_count, ud_prepost_threshold);

    if (ud_prepost_count < ud_prepost_threshold) {
        co_print("Preposting ud descriptors\n");
        while (ud_prepost_count < VIADEV_UD_PREPOST_DEPTH) {
            PREPOST_UD_VBUF_RECV();
            ud_prepost_count++;
        }
    }

    v = (vbuf *) vbuf_addr;

    co_print("[%d] received message: vbuf=%p\n", viadev.me, v);
    header = (viadev_packet_eager_start *) (VBUF_BUFFER_START(v) + 40);

    root = (header->envelope).src_lrank;
    Bcnt = (header->envelope.tag);
    co_print("ud msg:%d arrived\n", Bcnt);

    /* Drop packet received out of window ..Need to do even for rc */
    if ((Bcnt < viadev.bcast_info.Bcnt[root]) || (root == viadev.me)) {
        if (root == viadev.me)
            co_print("Dropping self pkt\n");
        else
            co_print("Dropping UD duplicate pkt\n");
        release_vbuf(v);
        return;
    }

    /* Search the unexpected Q and drop if an already existing pkt. is 
     * found */
    MPID_Search_unexpected_queue((header->envelope).src_lrank,
                                 (header->envelope).tag, -1, 0, &rhandle);
    if (rhandle) {
        co_print("Searched Q, dropping UD duplicate\n");
        release_vbuf(v);
        return;
    }


    MPID_Msg_arrived((header->envelope).src_lrank, (header->envelope).tag,
                     -1, &rhandle, &found);


    rhandle->s.count = header->envelope.data_length;
    rhandle->protocol = VIADEV_PROTOCOL_UD_MCST;
    co_print("Qing into unexpected Q in mrecv"
             ",protocol:%d\n", VIADEV_PROTOCOL_UD_MCST);
    rhandle->vbufs_received = 1;


    if (found) {
        char *vbuf_data =
            ((char *) header) + sizeof(viadev_packet_eager_start);

        /* recv posted already, check if recv buffer is large enough */
        if (header->envelope.data_length > rhandle->len) {
            printf("message truncated. ask %d got %d",
                   rhandle->len, header->envelope.data_length);
        }

        rhandle->len = header->envelope.data_length;
        memcpy((char *) (rhandle->buf), vbuf_data,
               header->bytes_in_this_packet);
        rhandle->bytes_copied_to_user = header->bytes_in_this_packet;

        assert(rhandle->bytes_copied_to_user == rhandle->len);
        RECV_COMPLETE(rhandle);
        release_vbuf(v);
        /*incrementing the count so that duplicates can be detected */
        ++viadev.bcast_info.Bcnt[root];

    } else {                    /*if not found */
        rhandle->len = header->envelope.data_length;
        rhandle->vbuf_head = v;
        rhandle->vbuf_tail = v;
        rhandle->bytes_copied_to_user = 0;

        /* add later */
        v->desc.next = NULL;
    }


}

#endif

/* Input: address of vbuf, which contains the send or recv decscriptor */
void viadev_process_send(void *vbuf_addr)
{
    vbuf *v;
    viadev_packet_header *header;
    viadev_connection_t *c = NULL;

    MPIR_SHANDLE *s;

    /* process a completed descriptor on the send queue.  
     * dispatch to an appropriate routine based on the type 
     * in the header field of the vbuf. xxx Later (with RDMA) we will 
     * have to handle packets that have immediate data 
     * but no packet header in the vbuf.  */

    v = (vbuf *) (vbuf_addr);

    /* since we have a completion, one more WQE is available
     * first process any existing WQEs on extended send q
     * if we implement send coallesing for RDMA fast Path (MAX_NO_COMPLETION)
     * we will need to handle >1 additions to send_wqes_avail
     */
    c = &(viadev.connections[v->grank]);
    c->send_wqes_avail++;
    if (c->ext_sendq_head) {
        viadev_ext_sendq_send(c);
    }

    /* RDMA read is being treated as a send completion.
     * The rationale is that it is tied to to the send work queue.
     * perhaps its not ok, to be treated in viadev_process_send,
     * rather viadev_process_recv. But anyways, it doesn't matter */


#ifdef ADAPTIVE_RDMA_FAST_PATH
    if (v->padding == RPUT_VBUF_FLAG) {
        release_vbuf(v);
        return;
    }
    if (v->padding == RGET_VBUF_FLAG) {
        process_rdma_read_completion(v);
        return;
    }

#else
    if (v->desc.u.sr.opcode == IBV_WR_RDMA_WRITE) {
        release_vbuf(v);
        return;
    }
    if (v->desc.u.sr.opcode == IBV_WR_RDMA_READ) {
        process_rdma_read_completion(v);
        return;
    }

#endif                          /* RDMA_FAST_PATH || ADAPTIVE_RDMA_FAST_PATH */


    header = (viadev_packet_header *) VBUF_BUFFER_START(v);

    switch (header->type) {
#ifdef ADAPTIVE_RDMA_FAST_PATH
#ifndef DISABLE_HEADER_CACHING
    case FAST_EAGER_CACHED:
#endif
#endif
    case VIADEV_PACKET_EAGER_COALESCE:
    case VIADEV_PACKET_EAGER_START:
    case VIADEV_PACKET_EAGER_NEXT:
    case VIADEV_PACKET_R3_DATA:
        s = (MPIR_SHANDLE *) (v->shandle);
        if (s != NULL)
            SEND_COMPLETE(s);
#ifdef ADAPTIVE_RDMA_FAST_PATH
        if (IS_NORMAL_VBUF(v)) {
            RELEASE_VBUF(v);
        } else {
            v->padding = FREE_FLAG;
            v->len = 0;
        }
#else
        RELEASE_VBUF(v);
#endif
#ifdef MEMORY_RELIABLE
        enqueue_crc_queue(v, c);
#endif
        break;

    case VIADEV_PACKET_RGET_FINISH:
        {
            MPIR_RHANDLE *r = NULL;
            r = (MPIR_RHANDLE *) v->shandle;
            if(viadev_use_nfr) {
                r->fin = NULL;

                if (0 == r->remote_address) {
                    /* it is dummy rhandle that was used for fin retransmission,
                     * just release it  */
                    MPID_RecvFree(r);
                    r = NULL;
                } else {
                    /* is not dummy rndv */
                    NFR_REMOVE_FROM_WAITING_LIST(&(c->rndv_inprocess), r);
                }
            }
            if (r != NULL) {
                RECV_COMPLETE(r);
            }
        }
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
        break;

    case VIADEV_PACKET_RPUT_FINISH:
        s = (MPIR_SHANDLE *) (v->shandle);
        if (s == NULL) {
            error_abort_all(GEN_ASSERT_ERR,
                            "s == NULL, s is the send "
                            "handler of the rput finish");
        }
        SEND_COMPLETE(s);
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
        break;

#if (defined(MCST_SUPPORT) || defined(ADAPTIVE_RDMA_FAST_PATH))
    case VIADEV_RDMA_ADDRESS:
#endif
    case VIADEV_PACKET_RENDEZVOUS_START_RETRY:
    case VIADEV_PACKET_RENDEZVOUS_START:
    case VIADEV_PACKET_RENDEZVOUS_REPLY:
        /* this one we will handle like eager messeges */
#ifdef ADAPTIVE_RDMA_FAST_PATH
        if (IS_NORMAL_VBUF(v)) {
            RELEASE_VBUF(v);
        } else { /* pasha , do we need the check at all ? */
            v->padding = FREE_FLAG;
            v->len = 0;
        }
#else
        RELEASE_VBUF(v);
#endif
        break;
    case VIADEV_PACKET_R3_ACK:
        RELEASE_VBUF(v);
        break;
#ifdef MCST_SUPPORT
    case VIADEV_PACKET_RENDEZVOUS_CANCEL:
#endif
    case VIADEV_PACKET_NOOP:
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
        break;
    case VIADEV_PACKET_BARRIER:
        D_PRINT("[%d] reaped barrier [%d]",
                viadev.me, ((viadev_packet_barrier *) header)->barrier_id);
#ifdef ADAPTIVE_RDMA_FAST_PATH
        if (v->padding == NORMAL_VBUF_FLAG) {
            release_vbuf(v);
        } else {
            v->padding = FREE_FLAG;
            v->len = 0;
        }
#else
#ifdef MEMORY_RELIABLE
        enqueue_crc_queue(v, c);
#else
        release_vbuf(v);
#endif
#endif
        break;
#ifdef MEMORY_RELIABLE
    case VIADEV_PACKET_CRC_ACK:
        release_vbuf(v);
        break;
#endif
    default:
        error_abort_all(IBV_STATUS_ERR,
                        "Unknown packet type %d in "
                        "viadev_process_send", header->type);
    }
}

static void viadev_handle_flow_control_srq(vbuf *v,
       viadev_connection_t *c, viadev_packet_header *header)
{
    pthread_spin_lock(&viadev.srq_post_spin_lock);

    viadev.posted_bufs--;

    if (VIADEV_UNLIKELY(viadev.posted_bufs <= viadev_credit_preserve)) {

        /* Need to post more to the SRQ */
        viadev.posted_bufs +=
            viadev_post_srq_buffers(viadev_srq_fill_size - viadev.posted_bufs);

    }

    pthread_spin_unlock(&viadev.srq_post_spin_lock);

    /* Check if we need to release the SRQ limit thread */
    if (viadev.srq_zero_post_counter >= viadev_srq_zero_post_max) {

        pthread_mutex_lock(&viadev.srq_post_mutex_lock);

        viadev.srq_zero_post_counter = 0;

        pthread_cond_signal(&viadev.srq_post_cond);

        pthread_mutex_unlock(&viadev.srq_post_mutex_lock);
    }
}

static void viadev_handle_flow_control_sr(vbuf *v,
        viadev_connection_t *c, viadev_packet_header *header)
{
    int needed, count;

    /* just consumed a vbuf from a VI receive queue, decrement prepost
     * count and check if we should re-post.  Want to do this here
     * since we may be sending a packet in response to this receive
     * and want it to include the credit update asap.
     */
#ifdef ADAPTIVE_RDMA_FAST_PATH
    if (v->padding == NORMAL_VBUF_FLAG) {
        c->preposts--;
    } 
#else
    c->preposts--;
#endif
    needed = viadev_prepost_depth + viadev_prepost_noop_extra
        + MIN(viadev_prepost_rendezvous_extra,
              c->rendezvous_packets_expected);

    if (c->initialized) {
#ifdef ADAPTIVE_RDMA_FAST_PATH
        if (header->type == VIADEV_PACKET_NOOP
            && v->padding == NORMAL_VBUF_FLAG) {
#else
        if (header->type == VIADEV_PACKET_NOOP) {
#endif
            PREPOST_VBUF_RECV(c);
            /* noops don't count for credits */
            c->local_credit--;
        } else if (c->preposts < (int) viadev_rq_size &&
                   c->preposts + viadev_prepost_threshold < needed) {
            count = 0;

            /* Collect all the vbufs into an array */
            do {
                collect_vbuf_for_recv(count, c);
                count++;
            } while (c->preposts < (int) viadev_rq_size
                     && c->preposts < needed);

            if (0 != count) {
                D_PRINT("posting %d recvs\n", count);
                viadev_post_recv_list(c, count);
            }
        }
    } else {
        int i;

        /* viadev.initialized means that MPI_Init has finished.
         *
         * Packets following that should "initialize" the
         * connection.
         * 
         * Even if MPI_Init has not yet finished, R3 packets
         * may have arrived, either as part of MPI_Init, or
         * rendezvous MPI sends of 0 bytes (Ssend). Incoming
         * rendezvous messages may have been discovered during
         * Device Check poll after the MPI_Init messages, but
         * before viadev.initialized is set by upper layer
         * in src/env/initutil.c 
         * In such case, we have to post buffers for the
         * connection, and move it to "initialized" state*/

        if ((!viadev.initialized &&
             (header->type == VIADEV_PACKET_RENDEZVOUS_START) &&
             (((viadev_packet_rendezvous_start *) header)->protocol
              == VIADEV_PROTOCOL_R3)) || viadev.initialized) {

            for (i = 0;
                 i <
                 viadev_prepost_noop_extra -
                 (viadev_initial_prepost_depth - viadev_initial_credits);
                 ++i) {
                PREPOST_VBUF_RECV(c);
                c->local_credit--;
            }


            /* fill out buffers to make up for small initial preposts */
            while (c->preposts < (int) viadev_rq_size
                   && c->preposts < needed) {
                PREPOST_VBUF_RECV(c);
            }

            c->initialized = 1;
        }
    }                           /* c->initialized */

    if (header->type == VIADEV_PACKET_R3_DATA) {
        D_PRINT("[%d]header->type=%d is R3 DATA\n", viadev.me,
                header->type);
        /* got an R3 Data packet, do we need to pre-post additional
         * receives?  This next call will ensure that we keep the
         * the number of additional pre-posts for R3 data sends
         * up to the maximum allowed.
         */
        if (c->rendezvous_packets_expected > 0) {
            viadev_prepost_for_rendezvous(c, 0);
        }
    }

    D_PRINT("VI %3d PKT RECV LC %3d PP %3d RC %3d CU %3d ",
            c->global_rank, c->local_credit,
            c->preposts, c->remote_credit, header->vbuf_credit);

    /* update our knowledge of remote sides credit count */
    c->remote_cc = header->remote_credit;

    /* accumulate vbuf credits and remember */
    if (header->vbuf_credit > 0) {
        c->remote_credit += header->vbuf_credit;
        if (header->type == VIADEV_PACKET_NOOP) {
            D_PRINT("VI %3d RECV NOOP with credit %d -- remote credit is now %d\n",
                    c->global_rank, header->vbuf_credit,
                    c->remote_credit);
        }
        if (c->backlog.len > 0) {
            /* We have pending messages on the backlog queue.
             * Send them out immediately.
             */
            viadev_backlog_send(c);
        }
        /* if any credits remain, schedule rendezvous progress */
        if ((c->remote_credit > 0) && (c->shandle_head != NULL)) {
            PUSH_FLOWLIST(c);
        }
    }
}
#ifdef XRC
void xrc_queue_packet (vbuf *v, int src_rank)
{
    xrc_queue_t *node = (xrc_queue_t *) malloc (xrc_queue_s);
    
    node->v = v;
    node->src_rank = src_rank;

    node->next = viadev.req_queue;
    node->prev = NULL;

    if (viadev.req_queue) 
        viadev.req_queue->prev = node;

    viadev.req_queue = node;

}
#endif
/*
 * The workhorse of CheckDevice
 */
void viadev_process_recv(void *vbuf_addr)
{
    vbuf *v;
    viadev_packet_header *header;
    viadev_connection_t *c;
    int was_nr = 0;
    int barrier_id;
    packet_sequence_t acks;  /* pending acks */

    /* *All* packets come IN through this routine and go OUT
     * through viadev_post_send. This is asymmetrical, so 
     * it doesn't feel right. But perhaps it's ok?
     */

    /* Process a completed descriptor on the recv queue.  dispatch to
     * an appropriate routine based on the type in the header field of
     * the vbuf. Flow control credits are always accumulated before
     * the packet is processed, and we check the sequence number.
     * When done, we release the vbuf and repost if necessary. 
     */
    v = (vbuf *) vbuf_addr;

    header = (viadev_packet_header *) VBUF_BUFFER_START(v);

#ifdef MEMORY_RELIABLE
    /* All packets are small, no RDMA */
    if (header->crc32 != update_crc(1, (void *)
                                    ((char *) v->buffer +
                                     sizeof(viadev_packet_header)),
                                    header->dma_len)) {

        fprintf(stderr, "[%d] Possible error in packet"
                " transmission: Checksum stamp %lu didn't match"
                " computed checksum %lu for msg len %d\n",
                viadev.me, header->crc32,
                update_crc(1, v->buffer, header->dma_len),
                header->dma_len);

        /* Send the NACK if the crc is not matching */
        if ((header->type != VIADEV_PACKET_CRC_ACK) && (header->type != VIADEV_PACKET_NOOP)){
            printf("%d sending nack with id :%d \n", viadev.me, header->id);
            MPID_VIA_ack_send(header->src_rank, header->id, 1);
            release_vbuf(v);
            return;
        }
    }
    else{

        c = &(viadev.connections[header->src_rank]);
        /* Drop the packet and send a NACK if out-of-order packet is received */
        if ((header->id > c->next_packet_expected)&& (header->type != VIADEV_PACKET_CRC_ACK) 
                && (header->type != VIADEV_PACKET_NOOP)) {
            MPID_VIA_ack_send(header->src_rank, header->id, 1);
            release_vbuf(v);
            return;
        } else {
            /* Send an ACK for packets other than the ACK itself */
            if ((header->type != VIADEV_PACKET_CRC_ACK) && (header->type != VIADEV_PACKET_NOOP)){
                MPID_VIA_ack_send(header->src_rank, header->id, 0);
            }
        }
    }

    /* Reset the checksum field in the buffer, to prevent
     * any `re-use' to accidentally use the older checksum */
    header->crc32 = 0;

#endif

#if !defined(DISABLE_HEADER_CACHING) && defined(ADAPTIVE_RDMA_FAST_PATH)
    if (header->type == FAST_EAGER_CACHED) {
        c = &(viadev.connections[v->grank]);
    } else {
        c = &(viadev.connections[header->src_rank]);
        v->grank = header->src_rank;    /* For all the other layers of code */
    }
#else
    c = &(viadev.connections[header->src_rank]);
    v->grank = header->src_rank; /* For all the other layers of code */
#endif /* !DISABLE_HEADER_CACHING && ADAPTIVE_RDMA_FAST_PATH */

#ifdef ADAPTIVE_RDMA_FAST_PATH
#ifndef DISABLE_HEADER_CACHING
    if (header->type == FAST_EAGER_CACHED) {
        c->cached_incoming.header.id = header->id;
        c->cached_incoming.header.type = FAST_EAGER_CACHED;
        c->cached_incoming.envelope.data_length = header->fast_eager_size;
        c->cached_incoming.header.fast_eager_size =
            header->fast_eager_size;
        header = &(c->cached_incoming.header);
    }
#endif
#endif

#ifdef ADAPTIVE_RDMA_FAST_PATH
    /* handle out of order messages for send/receive channel */
    /* RDMA channel don't return out of order message !!! */
    if (v->padding == NORMAL_VBUF_FLAG) {
        vbuf *v1;
        int ofo;
        int r1;
        while (header->type != VIADEV_PACKET_NOOP &&
                header->id != c->next_packet_expected) {
            if (VIADEV_UNLIKELY(c->progress_recov_mode && 
                        VIADEV_PACKET_RENDEZVOUS_START_RETRY == header->type)) {
                nfr_process_retransmit(c, header);
                return;
            }
            /* switch to the rdma channel */
#ifdef _SMP_
            if ((!disable_shared_mem) && SMP_INIT) {
                smpi_net_lookup();
            }
#endif
            r1 = poll_rdma_buffer((void *) &v1, &ofo);
            if (r1 == 0) {
                viadev_process_recv(v1);
            }
        }
    }
#endif

    if (VIADEV_LIKELY(header->type != VIADEV_PACKET_NOOP)) {
#ifndef ADAPTIVE_RDMA_FAST_PATH
        if (VIADEV_UNLIKELY(header->id != c->next_packet_expected)) {
            if(VIADEV_UNLIKELY(c->progress_recov_mode)) {
                nfr_process_retransmit(c, header);
                return;
            } else {
                error_abort_all(GEN_ASSERT_ERR,
                        "header->id (%d)!= "
                        "c->next_packet_expected (%d)",
                        header->id, c->next_packet_expected);
            }
        }
#endif
        /* assert (0 == viadev.progress_recov); */
        /* Why I commented the line below:
         * It is possible that during retransmission remote side will make post send to FIN message
         * and after it eager_rdma send. The FIN message will arrive AFTER eager RDMA beacause it pooled
         * first one. And as result the viadev.progress_recov will be 0. So we will swith of the recovery
         * mode becasue from this point we start to get messages in oder. The FIN message will be
         * processed later and it will take care for pending rndv requests */ 
        if (1 == c->progress_recov_mode) {
            c->progress_recov_mode = 0;
        }
        c->next_packet_expected++;
    }
    D_PRINT("[%d]header->type=%d\n", viadev.me, header->type);

    if(VIADEV_LIKELY(viadev_use_srq)) {
        /* Process SRQ re-posts only if *this* message
         * came from SRQ. This way we can save the
         * cost of a spin lock in the critical path */
#ifdef ADAPTIVE_RDMA_FAST_PATH
        if (v->padding == NORMAL_VBUF_FLAG) {
            viadev_handle_flow_control_srq(v, c, header);
        }
#else
        viadev_handle_flow_control_srq(v, c, header);
#endif
    } else {
        viadev_handle_flow_control_sr(v, c, header);
    }


#ifdef ADAPTIVE_RDMA_FAST_PATH
    /* process rdma credit */
    if (!viadev_use_nfr) {
        if (header->rdma_credit > 0) {
            c->ptail_RDMA_send += header->rdma_credit;
            if (c->ptail_RDMA_send >= viadev_num_rdma_buffer)
                c->ptail_RDMA_send -= viadev_num_rdma_buffer;
        }
    } else {
        /* pasha - move it to macro */
        uint16_t credit = header->rdma_credit;

        if (VIADEV_LIKELY(!(VIADEV_PACKET_NOOP == header->type && header->id > 0))) { /* pasha : I don't like the IF !!!!! */
            while (credit > 0) {
                --credit;
                ++(c->ptail_RDMA_send);

                if (c->ptail_RDMA_send >= viadev_num_rdma_buffer) {
                    c->ptail_RDMA_send -= viadev_num_rdma_buffer;
                }
                V_PRINT(DEBUG03, "Eager RDMA: Released ACKED Packet %s[%d][%s] list len [%d]\n",
                        type2name(VBUF_TYPE((&c->RDMA_send_buf[c->ptail_RDMA_send]))), 
                        VBUF_TYPE((&c->RDMA_send_buf[c->ptail_RDMA_send])), 
                        padding2name(c->RDMA_send_buf[c->ptail_RDMA_send].padding), 
                        WAITING_LIST_LEN((&c->waiting_for_ack)));
                /* we need the if because the vbuf maybe removed from the list during recovery
                 * I think we can prevent the if, i will check it later */
                if (VIADEV_LIKELY(NULL != c->RDMA_send_buf[c->ptail_RDMA_send].next)) {
                    WAITING_LIST_REMOVE((&c->waiting_for_ack),
                            &(c->RDMA_send_buf[c->ptail_RDMA_send]));
                }
            }
        }
    }
#endif
    /* caching ack becasue it maybe release during progress */
    acks = header->ack;

    switch (header->type) {
#ifdef ADAPTIVE_RDMA_FAST_PATH
#ifndef DISABLE_HEADER_CACHING
    case FAST_EAGER_CACHED:
        viadev_incoming_eager_start(v, c,
                (viadev_packet_eager_start *) header);
        break;
#endif
#endif
    case VIADEV_PACKET_EAGER_START:
#ifdef ADAPTIVE_RDMA_FAST_PATH
        FAST_PATH_ALLOC(c);
        if (viadev_use_nfr && IS_NORMAL_VBUF(v))
            NFR_INCREASE_PENDING(c);
#else
        NFR_INCREASE_PENDING(c);
#endif

        viadev_incoming_eager_start(v, c,
                                    (viadev_packet_eager_start *) header);
        /* dont release vbuf may have been unexpected. 
         * eager packet processing
         * will release vbufs when possible.  */
        break;
    case VIADEV_PACKET_EAGER_COALESCE:
#ifdef ADAPTIVE_RDMA_FAST_PATH
        if (viadev_use_nfr && IS_NORMAL_VBUF(v))
            NFR_INCREASE_PENDING(c);
#else
        NFR_INCREASE_PENDING(c);
#endif
        viadev_incoming_eager_coalesce(v, c,
                (viadev_packet_eager_coalesce *) header);
        break;

    case VIADEV_PACKET_EAGER_NEXT:
#ifdef ADAPTIVE_RDMA_FAST_PATH
        if (viadev_use_nfr && IS_NORMAL_VBUF(v))
            NFR_INCREASE_PENDING(c);
#else
        NFR_INCREASE_PENDING(c);
#endif
        viadev_incoming_eager_next(v, c,
                                   (viadev_packet_eager_next *) header);
        /* dont release vbuf may have been unexpected. 
         * eager packet processing
         * will release vbufs when possible. 
         */
        break;
    case VIADEV_PACKET_RENDEZVOUS_START:
#ifdef XRC
        if (c->vi == NULL && viadev_use_xrc) {
           /* Queue Packet */
           xrc_queue_packet (v, header->src_rank);
          
           /* Create / Find RC QP */
           MPICM_Connect_req (header->src_rank);
           break;
        }
        else 
#endif
        {
            viadev_incoming_rendezvous_start(v, c,
                (viadev_packet_rendezvous_start *) header);
        }
#ifdef ADAPTIVE_RDMA_FAST_PATH
        if (v->padding == NORMAL_VBUF_FLAG) {
            release_vbuf(v);
        } else {
            release_recv_rdma(c, v);
        }
#else
        release_vbuf(v);
#endif
        break;
    case VIADEV_PACKET_RENDEZVOUS_REPLY:
#ifdef MCST_SUPPORT
    case VIADEV_PACKET_RENDEZVOUS_CANCEL:
#endif
        NFR_INCREASE_PENDING(c);

        viadev_incoming_rendezvous_reply(v, c,
                                         (viadev_packet_rendezvous_reply *)
                                         header);
#ifdef ADAPTIVE_RDMA_FAST_PATH
        if (v->padding == NORMAL_VBUF_FLAG) {
            release_vbuf(v);
        } else {
            release_recv_rdma(c, v);
        }
#else
        release_vbuf(v);
#endif
        break;
    case VIADEV_PACKET_R3_DATA:
        /* R3 data can be delivered only via send protocol */
        NFR_INCREASE_PENDING(c);
        viadev_incoming_r3_data(v, c, (viadev_packet_r3_data *) header);
        release_vbuf(v);
        break;

#if (defined(MCST_SUPPORT) || defined(ADAPTIVE_RDMA_FAST_PATH))
    case VIADEV_RDMA_ADDRESS:

#ifdef MCST_SUPPORT
        if (is_mcast_enabled) {
            viadev.bcast_info.remote_add[c->global_rank] =
                (((viadev_packet_rdma_address *) header)->
                 RDMA_ACK_address);

            viadev.bcast_info.remote_rkey[c->global_rank] =
                (((viadev_packet_rdma_address *) header)->RDMA_ACK_hndl);
#ifndef RDMA_FAST_PATH
            c->remote_address_received = 1;
#endif
        }
#endif


#ifdef ADAPTIVE_RDMA_FAST_PATH
        viadev_incoming_rdma_address(v, c, (viadev_packet_rdma_address *)
                                     header);

        if (v->padding == NORMAL_VBUF_FLAG) {
            release_vbuf(v);
        } else {
            release_recv_rdma(c, v);
        }
#else
        release_vbuf(v);
#endif
        break;
#endif
    case VIADEV_PACKET_RPUT_FINISH:
        viadev_incoming_rput_finish(v, c,
                                    (viadev_packet_rput_finish *) header);
#ifdef ADAPTIVE_RDMA_FAST_PATH
        if (v->padding == NORMAL_VBUF_FLAG) {
            release_vbuf(v);
        } else {
            release_recv_rdma(c, v);
        }
#else
        release_vbuf(v);
#endif
        break;
    case VIADEV_PACKET_NOOP:
        /* Handling NFR reconnect request.
         * In NOOP messages the ID value is never used - it is alway zero 
         * I will use it for our NFR reconnect request and reply stuff
         * Yep, it is ugly, from other side it will save *if* latency in a lot of places
         * and also will prevent a lot of ifdefs
         */
        if (viadev_use_nfr) {
            switch(header->id) {
                case NFR_REQ:
                    nfr_incoming_nfr_req(header, c);
                    was_nr = 1;
                    break;
                case NFR_REP:
                    nfr_incoming_nfr_rep(header, c);
                    was_nr = 1;
                    break;
                case NFR_FIN:
                    V_PRINT(DEBUG03,"Got NOOP Fin Message\n");
                    nfr_incoming_nfr_fin(c);
                    c->progress_recov_mode = 0;
                    nfr_fatal_error = NO_FATAL;
                    was_nr = 1;
                    break;
                case 0:
                    /* Ok, it is usual noop message */
                    break;
                default:
                    error_abort_all(GEN_EXIT_ERR,
                            "Unknown NOOP packet ID %d", header->id);
            }
        }
#ifdef ADAPTIVE_RDMA_FAST_PATH
        if (v->padding == NORMAL_VBUF_FLAG) {
            release_vbuf(v);
        } else {
            release_recv_rdma(c, v);
        }
#else
        release_vbuf(v);
#endif
        if (1 == was_nr) {
            return;
        }
        break;
    case VIADEV_PACKET_BARRIER:
        barrier_id = ((viadev_packet_barrier *) header)->barrier_id;
        D_PRINT("[%d] got barrier [%d] from [%d]",
                viadev.me, barrier_id, c->global_rank);
        c->barrier_id = barrier_id;
#ifdef ADAPTIVE_RDMA_FAST_PATH
        if (v->padding == NORMAL_VBUF_FLAG) {
            release_vbuf(v);
        } else {
            release_recv_rdma(c, v);
        }
#else
        release_vbuf(v);
#endif
        break;

    case VIADEV_PACKET_RGET_FINISH:
        viadev_incoming_rget_finish(v, c,
                                    (viadev_packet_rget_finish *) header);
#ifdef ADAPTIVE_RDMA_FAST_PATH
        if (v->padding == NORMAL_VBUF_FLAG) {
            release_vbuf(v);
        } else {
            release_recv_rdma(c, v);
        }
#else
        release_vbuf(v);
#endif                          /* RDMA_FAST_PATH */
        break;

#ifdef MEMORY_RELIABLE
    case VIADEV_PACKET_CRC_ACK:
        viadev_process_ack(v, c, (viadev_packet_crc_ack*) header);
        release_vbuf(v);
        break;
#endif
    case VIADEV_PACKET_R3_ACK:
        NFR_INCREASE_PENDING(c);
        viadev_process_r3_ack(v, c, (viadev_packet_r3_ack *) header);
        release_vbuf(v);
        break;

    default:
        error_abort_all(IBV_STATUS_ERR,
                        "Unknown packet type %d in "
                        "viadev_process_recv", header->type);
    }

    NFR_RELEASE(&c->waiting_for_ack, acks);
    viadev_send_noop_ifneeded(c);
    NFR_SEND_ACK_IFNEEDED(c);
}


#ifdef ADAPTIVE_RDMA_FAST_PATH
void process_remote_rdma_address(viadev_connection_t * c,
                                 uint32_t remote_hndl,
                                 vbuf * remote_address);

void viadev_incoming_rdma_address(vbuf * v,
                                  viadev_connection_t * c,
                                  viadev_packet_rdma_address * header)
{
    /* address also received */
    if (c->remote_address_received != 0) {
        error_abort_all(GEN_EXIT_ERR,
                        "global rank for this connection: %d",
                        c->global_rank);
    }
    process_remote_rdma_address(c, header->RDMA_hndl,
                                header->RDMA_address);

}
#endif


void viadev_incoming_rendezvous_start(vbuf * v, viadev_connection_t * c,
                                      viadev_packet_rendezvous_start *
                                      header)
{

    MPIR_RHANDLE *rhandle;
    int found;

#ifdef MCST_SUPPORT

    MPIR_RHANDLE *rhandle1;
    int root, Bcnt;
    co_print("Entered incoming rendezvous start\n");
    root = header->envelope.src_lrank;

    if (header->envelope.context == -1) {
        Bcnt = (header->envelope.tag);

        /* Drop packet received out of window ..Need to do even for rc */
        if (Bcnt < viadev.bcast_info.Bcnt[root]) {
            co_print("Bcnt:%d, viadev.bcast_info.Bcnt:%d\n",
                     Bcnt, viadev.bcast_info.Bcnt[root]);
            co_print("Dropping duplicate pkt in rendezvous start\n");
            co_print("Cancelling rendezvous\n");
            MPID_RecvAlloc(rhandle1);
            rhandle1->connection = c;
            rhandle1->send_id = header->sreq;
            viadev_rendezvous_cancel(rhandle1);
            return;
        }
        /* Search the unexpected Q and drop if an already existing pkt. is 
         * found */
        MPID_Search_unexpected_queue((header->envelope).src_lrank,
                                     (header->envelope).tag,
                                     -1, 0, &rhandle);
        if (rhandle) {

            co_print("Searched Q, dropping duplicate in rendezvous\n");
            co_print("Cancelling rendezvous\n");
            MPID_RecvAlloc(rhandle1);
            rhandle1->connection = c;
            rhandle1->send_id = header->sreq;
            viadev_rendezvous_cancel(rhandle1);
            return;
        }
    }

    co_print("in rend. start:src:%d,tag:%d,context:%d\n",
             header->envelope.src_lrank, header->envelope.tag,
             header->envelope.context);
#endif

    MPID_Msg_arrived(header->envelope.src_lrank,
                     header->envelope.tag,
                     header->envelope.context, &rhandle, &found);

    /* This information needs to be filled in whether a preposted 
     * receive was found or not.  */

    rhandle->connection = c;
    rhandle->send_id = header->sreq;
    rhandle->s.count = header->envelope.data_length;
    rhandle->vbufs_received = 0;
    rhandle->vbuf_head = NULL;
    rhandle->vbuf_tail = NULL;
    rhandle->replied = 0;
    rhandle->protocol = header->protocol;

    rhandle->remote_address = header->buffer_address;
    rhandle->remote_memhandle_rkey = header->memhandle_rkey;
    if(viadev_use_nfr) {
        rhandle->fin  = NULL;
        rhandle->next = NULL;
        rhandle->prev = NULL;
        rhandle->was_retransmitted = 0;
        rhandle->sn = header->header.id;
    }

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
        case VIADEV_PROTOCOL_R3:
            viadev_recv_r3(rhandle);
            break;
        case VIADEV_PROTOCOL_RGET:
            viadev_recv_rget(rhandle);
            break;
        case VIADEV_PROTOCOL_RPUT:
            viadev_recv_rput(rhandle);
            break;
        case VIADEV_PROTOCOL_RENDEZVOUS_UNSPECIFIED:
            error_abort_all(IBV_STATUS_ERR,
                            "VIRS: unspecified protocol %d",
                            rhandle->protocol);
            break;
        default:
            error_abort_all(IBV_STATUS_ERR,
                            "VIRS: invalid protocol %d",
                            rhandle->protocol);
        }
    }
}

void viadev_incoming_eager_coalesce(vbuf * v, viadev_connection_t * c,
        viadev_packet_eager_coalesce * coalesced_header)
{
    viadev_packet_eager_coalesce_part *ph;
    viadev_packet_eager_coalesce_full *fh;
    viadev_packet_envelope * envelope = NULL;

    MPIR_RHANDLE *rhandle;
    int found, i, unexpected = 0;
    char *data_ptr;

    data_ptr = ((char *) coalesced_header) + sizeof(viadev_packet_eager_coalesce);

    D_PRINT("pkt count: %d\n", coalesced_header->pkt_count);
    for(i = 0; i < coalesced_header->pkt_count; i++) {

        /* figure out what type we have (cached or not) */

        ph = (viadev_packet_eager_coalesce_part *) data_ptr;
        if(ph->coalesce_type == COALESCED_CACHED) {
            envelope = &c->coalesce_cached_in;
            data_ptr = data_ptr + sizeof(*ph);
        } else if(ph->coalesce_type == COALESCED_NOT_CACHED) {
            fh = (viadev_packet_eager_coalesce_full *) data_ptr;
            envelope = &fh->envelope;
            memcpy(&(c->coalesce_cached_in), envelope, sizeof(*envelope));
            data_ptr = data_ptr + sizeof(*fh);
        } else {
            error_abort_all(IBV_STATUS_ERR, 
                "Unknown cached val: %d", ph->coalesce_type);
        }

        /* now see if the recv is posted */

        MPID_Msg_arrived(envelope->src_lrank,
                envelope->tag,
                envelope->context, &rhandle, &found);

        /* This information needs to be filled in whether a preposted
         * receive was found or not.
         */

        rhandle->connection = c;
        rhandle->s.count = envelope->data_length;

        rhandle->protocol = VIADEV_PROTOCOL_EAGER;
        rhandle->vbufs_received = 1;

        if (found) {
            int truncated = 0;
            char *vbuf_data = data_ptr;
            /* recv posted already, check if recv buffer is large enough */
            if (envelope->data_length > rhandle->len) {
                truncated = 1;
            } else {
                rhandle->len = envelope->data_length;
            }

            /* matching packet is here, can no longer cancel recv */
            rhandle->can_cancel = 0;

            rhandle->vbufs_expected =
                viadev_calculate_vbufs_expected(rhandle->len,
                        rhandle->protocol);

            if (rhandle->len > 0) {
                memcpy((char *) (rhandle->buf), vbuf_data, rhandle->len);
            }

            rhandle->bytes_copied_to_user = rhandle->len;
            if (rhandle->vbufs_expected == 1) {
                RECV_COMPLETE(rhandle);

                if (truncated) {
                    rhandle->s.MPI_ERROR = MPI_ERR_TRUNCATE;
                }

                D_PRINT("VI %3d EAGER START RECV COMPLETE, total %d",
                        c->global_rank, rhandle->s.count);
            } else {
                c->rhandle = rhandle;
                rhandle->s.MPI_ERROR =
                    truncated ? MPI_ERR_TRUNCATE : MPI_SUCCESS;
            }
        } else {
            /* Recv not yet posted, set size of incoming data in
             * unexpected queue rhandle
             */

            D_PRINT("Got an unexpected message!\n\n");

            rhandle->len = envelope->data_length;
            rhandle->vbuf_head = v;
            rhandle->vbuf_tail = v;
            rhandle->bytes_copied_to_user = 0;
            v->desc.next = NULL;
            c->rhandle = rhandle;

            rhandle->protocol = VIADEV_PROTOCOL_EAGER_COALESCE;
            rhandle->coalesce_data_buf = data_ptr;

            unexpected++;
        }

        data_ptr += envelope->data_length;
        D_PRINT("done with %d\n", i);
    }

    /* need to keep this vbuf around until all recvs are posted
     * that are associated with this vbuf
     */
    D_PRINT("done with all packets\n");

    if(unexpected > 0) {
        v->ref_count = unexpected;
    } else {
#ifdef ADAPTIVE_RDMA_FAST_PATH
        if (v->padding == NORMAL_VBUF_FLAG)
            release_vbuf(v);
        else
            release_recv_rdma(c, v);
#else
        release_vbuf(v);
#endif
    }

    D_PRINT("done with processing send\n");

}


void viadev_incoming_eager_start(vbuf * v, viadev_connection_t * c,
                                 viadev_packet_eager_start * header)
{

    MPIR_RHANDLE *rhandle;
    int found;

#ifdef MCST_SUPPORT
    int root, Bcnt;

    root = 0;

    co_print("Entering incoming eager start \n");
    if (header->envelope.context == -1) {

        root = header->envelope.src_lrank;
        Bcnt = (header->envelope.tag);
        co_print("Bcnt:%d,bcast_info.Bcnt:%d\n", Bcnt,
                 viadev.bcast_info.Bcnt[root]);

        /* Drop packet received out of window ..Need to do even for rc */
        if (Bcnt < viadev.bcast_info.Bcnt[root]) {
            co_print("Dropping duplicate pkt in eager start\n");
#if defined(RDMA_FAST_PATH)
            if (v->padding == NORMAL_VBUF_FLAG)
                release_vbuf(v);
            else
                release_recv_rdma(c, v);
#else
            release_vbuf(v);
#endif
            return;
        }

        /* Search the unexpected Q and drop if an already existing pkt. is 
         * found */
        MPID_Search_unexpected_queue((header->envelope).src_lrank,
                                     (header->envelope).tag,
                                     -1, 0, &rhandle);
        if (rhandle) {
            co_print("Searched Q, dropping duplicate\n");
#if defined(RDMA_FAST_PATH)
            if (v->padding == NORMAL_VBUF_FLAG)
                release_vbuf(v);
            else
                release_recv_rdma(c, v);
#else
            release_vbuf(v);
#endif
            return;
        }
    }
#endif

    MPID_Msg_arrived(header->envelope.src_lrank,
                     header->envelope.tag,
                     header->envelope.context, &rhandle, &found);

#ifdef ADAPTIVE_RDMA_FAST_PATH
#ifndef DISABLE_HEADER_CACHING
    /* caching header */
    if ((header->header.type == VIADEV_PACKET_EAGER_START) &&
        (header->envelope.data_length < viadev_max_fast_eager_size) &&
        (v->padding != NORMAL_VBUF_FLAG)) {
        memcpy(&(c->cached_incoming), header,
               sizeof(viadev_packet_eager_start));
    }
#endif
#endif

    /* This information needs to be filled in whether a preposted 
     * receive was found or not. 
     */

    rhandle->connection = c;
    rhandle->s.count = header->envelope.data_length;

    rhandle->protocol = VIADEV_PROTOCOL_EAGER;
    rhandle->vbufs_received = 1;

    if (found) {

        int truncated = 0;
        int copy_bytes;
#if (!defined(DISABLE_HEADER_CACHING) && \
        defined(ADAPTIVE_RDMA_FAST_PATH))
        char *vbuf_data;
#else
        char *vbuf_data = ((char *) header) +
            sizeof(viadev_packet_eager_start);
#endif
        /* recv posted already, check if recv buffer is large enough */
        if (header->envelope.data_length > rhandle->len) {
            truncated = 1;
        } else {
            rhandle->len = header->envelope.data_length;
        }

#ifdef ADAPTIVE_RDMA_FAST_PATH

#if (!defined(DISABLE_HEADER_CACHING))
        if (header->header.type == FAST_EAGER_CACHED) {
            vbuf_data =
                ((char *) (VBUF_BUFFER_START(v))) + FAST_EAGER_HEADER_SIZE;
            header->bytes_in_this_packet = header->header.fast_eager_size;
            copy_bytes = rhandle->len;
        } else {
            vbuf_data = ((char *) header) +
                sizeof(viadev_packet_eager_start);
            copy_bytes = MIN(rhandle->len, header->bytes_in_this_packet);
        }
#else
        copy_bytes = MIN(rhandle->len, header->bytes_in_this_packet);
#endif

#else
        copy_bytes = MIN(rhandle->len, header->bytes_in_this_packet);
#endif

        /* matching packet is here, can no longer cancel recv */
        rhandle->can_cancel = 0;

        rhandle->vbufs_expected =
            viadev_calculate_vbufs_expected(rhandle->len,
                                            rhandle->protocol);

        if (copy_bytes > 0) {
            memcpy((char *) (rhandle->buf), vbuf_data, copy_bytes);
        }

        rhandle->bytes_copied_to_user = copy_bytes;
        if (VIADEV_LIKELY(rhandle->vbufs_expected == 1)) {
            RECV_COMPLETE(rhandle);
#ifdef MCST_SUPPORT
            /* Incrementing the count so that duplicates can be found */
            if (header->envelope.context == -1) {
                ++viadev.bcast_info.Bcnt[root];
            }
#endif
            if (VIADEV_UNLIKELY(truncated)) {
                rhandle->s.MPI_ERROR = MPI_ERR_TRUNCATE;
            }

            D_PRINT("VI %3d EAGER START RECV COMPLETE, total %d",
                    c->global_rank, rhandle->s.count);
        } else {
            c->rhandle = rhandle;
            rhandle->s.MPI_ERROR =
                truncated ? MPI_ERR_TRUNCATE : MPI_SUCCESS;
        }
#ifdef ADAPTIVE_RDMA_FAST_PATH
        if (v->padding == NORMAL_VBUF_FLAG)
            release_vbuf(v);
        else
            release_recv_rdma(c, v);
#else
        release_vbuf(v);
#endif
    } else {
        /* Recv not yet posted, set size of incoming data in
         * unexpected queue rhandle
         */
        rhandle->len = header->envelope.data_length;
        rhandle->vbuf_head = v;
        rhandle->vbuf_tail = v;
        rhandle->bytes_copied_to_user = 0;
        v->desc.next = NULL;
        c->rhandle = rhandle;
    }
}

void viadev_incoming_eager_next(vbuf * v, viadev_connection_t * c,
                                viadev_packet_eager_next * header)
{
    MPIR_RHANDLE *rhandle = c->rhandle;
    int copy_bytes = 0;

    if (rhandle == NULL) {
        error_abort_all(GEN_ASSERT_ERR, "rhandle is NULL");
    }

    rhandle->vbufs_received++;

    if (rhandle->bytes_copied_to_user > 0) {
        /* this is a user receive handle, and has already been matched */
        char *vbuf_data = ((char *) header) +
            sizeof(viadev_packet_eager_next);

        copy_bytes = MIN(header->bytes_in_this_packet,
                         (rhandle->len - rhandle->bytes_copied_to_user));

        if (copy_bytes) {
            memcpy((char *) (rhandle->buf) +
                   rhandle->bytes_copied_to_user, vbuf_data, copy_bytes);
            rhandle->bytes_copied_to_user += copy_bytes;
        }

        if (rhandle->vbufs_expected == rhandle->vbufs_received) {
            int err = rhandle->s.MPI_ERROR;
            RECV_COMPLETE(rhandle);
            rhandle->s.MPI_ERROR = err;
            D_PRINT("VI %3d EAGER NEXT RECV COMPLETE, total %d",
                    c->global_rank, rhandle->s.count);
        }
#ifdef ADAPTIVE_RDMA_FAST_PATH
        if (v->padding == NORMAL_VBUF_FLAG)
            release_vbuf(v);
        else
            release_recv_rdma(c, v);
#else
        release_vbuf(v);
#endif
    } else {
        /* haven't posted the message yet. accumulate vbufs on a chain */
        ((vbuf *) rhandle->vbuf_tail)->desc.next = v;
        v->desc.next = NULL;
        rhandle->vbuf_tail = v;
    }
}

/* 
 * We previously initiated a rendezvous send. 
 * Receiver has posted a matching receive and is 
 * telling us about it. 
 * The fact that they are telling us about it means
 * it is not an rget. It is either an r3 or an rput
 * depending on whether we are sending data directly 
 * into the user buffer on the other side
 * For the first implementation, only r3 is supported, 
 * copied through vbufs on both sides. 
 * 
 * Since messages can be very large and vbuf resources
 * are limited, we have to handle the case where there
 * are not enough preposted vbufs on the other side
 * to handle our data (this is true even for rput, 
 * which still consumes vbufs). 
 * 
 * For a non-pathological receiver (philosophical
 * debates about the MPI progress rule being of little
 * relevance to the real world -- and for the record we 
 * state that in the initial single-threaded imlementation 
 * we do not obey the strongest version of the rule)
 * we will receive credit renewals that allow us
 * to proceed. However, to get those credits, we have
 * to process some incoming packets. To handle this, we
 * put send handles for started-but-not-completed
 * rendezvous transfers on a queue of send
 * handles attached to the connection. MPID_DeviceCheck
 * makes progress on these through "viadev_rendezvous_push_transfer"
 * when it has run out of packets to process. 
 * 
 * All we do in this routine is set up the send handle and 
 * defer actually pushing data until all packets are processed. 
 *
 */

void viadev_incoming_rendezvous_reply(vbuf * v, viadev_connection_t * c,
                                      viadev_packet_rendezvous_reply *
                                      header)
{
    MPIR_SHANDLE *s;
    /* first make sure we have a match */
    s = (MPIR_SHANDLE *) (ID_TO_REQ(header->sreq));

    if (s == NULL) {
        error_abort_all(GEN_ASSERT_ERR,
                        "s == NULL, send handler at viadev_incoming_rendezvous_reply");
    }
#ifdef MCST_SUPPORT

    if (header->header.type == VIADEV_PACKET_RENDEZVOUS_CANCEL) {
        SEND_COMPLETE(s);
        return;
    }
#endif

    /* fill in some fields */
    s->bytes_sent = 0;
    s->receive_id = header->rreq;


    /* we are the sender and initially set a protocol.  The
     * receiver may not be able to support our protocol and
     * may have reverted to R3.  If so, we have to revert as
     * well, and unregister any memory */

    switch (s->protocol) {
    case VIADEV_PROTOCOL_RPUT:
        if (header->protocol == VIADEV_PROTOCOL_R3) {
            if (s->dreg_entry != NULL) {
                dreg_unregister(s->dreg_entry);
                s->dreg_entry = NULL;
            }
            s->remote_address = NULL;
            s->protocol = VIADEV_PROTOCOL_R3;
            /* If receiver wants to switch to R3 we need to release the RNDV START now */
            NFR_RELEASE_RNDV_START(&c->waiting_for_ack, (vbuf*)s->rndv_start);
        } else {
            assert(header->protocol == VIADEV_PROTOCOL_RPUT);

            s->remote_address = header->buffer_address;
            s->remote_memhandle_rkey = header->memhandle_rkey;
        }
        break;
    case VIADEV_PROTOCOL_RGET:
        if (header->protocol == VIADEV_PROTOCOL_R3) {
            s->protocol = VIADEV_PROTOCOL_R3;
            /* If receiver wants to switch to R3 we need to release the RNDV START now */
            NFR_RELEASE_RNDV_START(&c->waiting_for_ack, (vbuf*)s->rndv_start);
        } else {
            /* should never have gotten here if both sides
             * agreed on RGET */
            error_abort_all(IBV_STATUS_ERR, "VIRR: RGET protocol failure");
        }
        break;

    case VIADEV_PROTOCOL_R3:
        assert(header->protocol == VIADEV_PROTOCOL_R3);
        /* We got reply on our rndv request, so we can remove it from the list */
        NFR_RELEASE_RNDV_START(&c->waiting_for_ack, (vbuf*)s->rndv_start);
        break;

    default:
        error_abort_all(IBV_STATUS_ERR,
                        "VIRR: invalid sender protocol [%d]", s->protocol);
    }

    /* 
     * even if we have an rput, which we know we could finish, 
     * put it on the list so that it is processed in order. 
     * Need to satisfy MPI ordering requirements. 
     */

    D_PRINT("VI %3d R3 OKTS RECVED len %d",
            c->global_rank, s->bytes_total);

    RENDEZVOUS_IN_PROGRESS(c, s);

    /*
     * this is where all rendezvous transfers are started,
     * so it is the only place we need to set this kludgy
     * field
     */

    s->nearly_complete = 0;

    PUSH_FLOWLIST(c);

}


static void process_flowlist(void)
{
    MPIR_SHANDLE *s;

    while (flowlist) {

        /* Push on the the first ongoing receive with 
         * viadev_rendezvous_push. If the receive
         * finishes, it will advance the shandle_head
         * pointer on the connection. 
         * 
         * xxx the side effect of viadev_rendezvous_push is
         * bad practice. Find a way to do this so the logic
         * is obvious. 
         */

        s = flowlist->shandle_head;
        while (s != NULL) {
            viadev_rendezvous_push(s);
            if (!s->nearly_complete) {
                break;
            }
            RENDEZVOUS_DONE(flowlist);
            s = flowlist->shandle_head;
        }
        /* now move on to the next connection */
        POP_FLOWLIST();
    }
}


void viadev_rendezvous_push(MPIR_SHANDLE * s)
{
    vbuf *v;
    viadev_packet_r3_data *h;
    char *databuf;
    int datamax = VBUF_DATA_SIZE(viadev_packet_r3_data);

    viadev_connection_t *c = (viadev_connection_t *) s->connection;


    /* Perform the manual transition of APM only if both APM and the APM
     * Tester are defined */

    if(viadev_use_apm && viadev_use_apm_test) {
        perform_manual_apm(c->vi);
    }

    if (s->protocol == VIADEV_PROTOCOL_RPUT) {
        int nbytes;
        if (s->bytes_sent != 0) {
            error_abort_all(GEN_ASSERT_ERR,
                            "s->bytes_sent != 0 Rendezvous Push");
        }

        if (s->bytes_total > 0) {
            assert(s->dreg_entry != NULL);
            assert(s->remote_address != NULL);
        }
        while (s->bytes_sent < s->bytes_total) {
            v = get_vbuf();
            assert(v != NULL);
            nbytes = s->bytes_total - s->bytes_sent;
            if (nbytes > (int) viadev.maxtransfersize) {
                nbytes = viadev.maxtransfersize;
            }
            viadev_rput(c, v,
                        (char *) (s->local_address) + s->bytes_sent,
                        ((dreg_entry *) s->dreg_entry)->memhandle->lkey,
                        (char *) (s->remote_address) + s->bytes_sent,
                        s->remote_memhandle_rkey, nbytes);
            s->bytes_sent += nbytes;
        }
        assert(s->bytes_sent == s->bytes_total);

        viadev_rput_finish(s);
        s->nearly_complete = 1;

        return;
    }

    /* if we get here, then protocol is R3 */

    while ((viadev_use_srq || c->remote_credit >= viadev_credit_preserve) &&
           s->nearly_complete == 0) {

        if(viadev_use_srq) {
            if(c->pending_r3_data >= viadev_max_r3_pending_data) {
                break;
            }
        }

        v = get_vbuf();

        /* figure out where header and data are */
        h = (viadev_packet_r3_data *) VBUF_BUFFER_START(v);
        databuf = ((char *) h) + sizeof(viadev_packet_r3_data);

        /* set up the packet */
        PACKET_SET_HEADER_NFR(h, c, VIADEV_PACKET_R3_DATA);
        h->rreq = s->receive_id;

        /* figure out how much data to send in this packet */
        h->bytes_in_this_packet = s->bytes_total - s->bytes_sent;
        if (datamax < h->bytes_in_this_packet) {
            h->bytes_in_this_packet = datamax;
        }

        /* copy next chunk of data in to vbuf */
        memcpy(databuf, ((char *) s->local_address) +
               s->bytes_sent, h->bytes_in_this_packet);

        s->bytes_sent += h->bytes_in_this_packet;

        /* finally send it off */
        vbuf_init_send(v, sizeof(viadev_packet_r3_data) +
                       h->bytes_in_this_packet);
        /* If this is the last packet, mark the vbuf so that the MPI
         * send is marked complete when the VIA send completes. 
         * Do not remove from the shandle list on the connection. Whoever
         * called us will do that. 
         */
        if (s->bytes_sent == s->bytes_total) {
            /* mark MPI send complete when VIA send completes */
            v->shandle = s;
            s->nearly_complete = 1;
        }

        viadev_post_send(c, v);

        if(viadev_use_srq) {
            c->pending_r3_data += h->bytes_in_this_packet;
        }
    }
    if (s->nearly_complete) {
        D_PRINT("VI %3d R3 PUSH COMPLETE, total %d",
                c->global_rank, s->bytes_total);
    } else {
        D_PRINT("VI %3d R3 PUSH STALLED, total %d, sent %d\n",
                c->global_rank, s->bytes_total, s->bytes_sent);
    }
}



void viadev_incoming_r3_data(vbuf * v, viadev_connection_t * c,
                             viadev_packet_r3_data * h)
{

    MPIR_RHANDLE *rhandle;
    void *databuf;
    int bytes_to_copy;

    /* find the rhandle for this data */
    rhandle = (MPIR_RHANDLE *) ID_TO_REQ(h->rreq);

    databuf = ((char *) h) + sizeof(viadev_packet_r3_data);
    bytes_to_copy = h->bytes_in_this_packet;

    rhandle->vbufs_received++;

    memcpy(((char *) rhandle->buf) +
           rhandle->bytes_copied_to_user, databuf, bytes_to_copy);

    rhandle->bytes_copied_to_user += bytes_to_copy;

    c->rendezvous_packets_expected--;

    /* sanity check */
    assert(rhandle->bytes_copied_to_user <= rhandle->len);

    if (rhandle->bytes_copied_to_user == rhandle->len) {
        assert(rhandle->vbufs_received == rhandle->vbufs_expected);
        RECV_COMPLETE(rhandle);
        D_PRINT("VI %3d R3 RECV COMPLETE total %d",
                c->global_rank, rhandle->len);
    }

    if(viadev_use_srq) {
        c->received_r3_data += bytes_to_copy;

        if(c->received_r3_data >= viadev_max_r3_pending_data) {
            viadev_r3_ack(c);
        }
    }
}


void viadev_incoming_rput_finish(vbuf * v, viadev_connection_t * c,
                                 viadev_packet_rput_finish * h)
{

    MPIR_RHANDLE *rhandle;

    /* find the rhandle for this data */
    rhandle = (MPIR_RHANDLE *) ID_TO_REQ(h->rreq);

    rhandle->bytes_copied_to_user = rhandle->len;
    RECV_COMPLETE(rhandle);

}

void viadev_incoming_rget_finish(vbuf * v, viadev_connection_t * c,
                                 viadev_packet_rget_finish * h)
{
    MPIR_SHANDLE *shandle;

    /* find the rhandle for this data */
    shandle = (MPIR_SHANDLE *) ID_TO_REQ(h->sreq);
    NFR_RELEASE_RNDV_START(&c->waiting_for_ack, (vbuf*)shandle->rndv_start);
    SEND_COMPLETE(shandle);
}

void viadev_send_noop_ifneeded(viadev_connection_t * c)
{
    if(!viadev_use_srq) {
        /* Dont want remote end to stall too long waiting for a credit
         * update.  If the local credits have built up above the dynamic
         * threshold, send a no-op immediately
         * c->remote_cc should accurately reflect the remote sides credit situation.
         * If its low and we have built-up local credits, send a noop
         * for the purposes of a credit refresh and to prevent a deadlock.
         */

        D_PRINT("local credit: %d, dynamic: %d, remote_cc: %d, preserve: %d, notify: %d, finalized: %d\n",
                c->local_credit, viadev_dynamic_credit_threshold,
                c->remote_cc, viadev_credit_preserve,
                viadev_credit_notify_threshold, viadev.is_finalized);

        if ((c->local_credit >= viadev_dynamic_credit_threshold
                    || (c->remote_cc <= viadev_credit_preserve &&
                        c->local_credit >= viadev_credit_notify_threshold)
            ) && !viadev.is_finalized) {
            viadev_send_noop(c);
        }
    }
}

void viadev_send_noop(viadev_connection_t * c)
{
    /* always send a noop when it is needed even if there is a backlog.
     * noops do not consume credits.
     * this is necessary to avoid credit deadlock.
     * RNR NAK will protect us if receiver is low on buffers.
     * by doing this we can force a noop ahead of any other queued packets.
     */

    /* could potentially use RDMA channel here */

    vbuf *v = get_vbuf();
    viadev_packet_noop *p = (viadev_packet_noop *) VBUF_BUFFER_START(v);
    D_PRINT("VI %3d SEND NOOP with %d credits\n", c->global_rank,
            c->local_credit);
    PACKET_SET_HEADER_NOOP(p, c);
    vbuf_init_send(v, sizeof(viadev_packet_header));
    viadev_post_send(c, v);

}

void viadev_r3_ack(viadev_connection_t *c)
{
    vbuf *v = get_vbuf();

    viadev_packet_r3_ack *p = (viadev_packet_r3_ack *) VBUF_BUFFER_START(v);

    PACKET_SET_HEADER_NFR(p, c, VIADEV_PACKET_R3_ACK);

    p->ack_data = c->received_r3_data;

    c->received_r3_data = 0;

    vbuf_init_send(v, sizeof(viadev_packet_r3_ack));

    viadev_post_send(c, v);
}

void viadev_rput_finish(MPIR_SHANDLE * s)
{
    vbuf *v = get_vbuf();
    void *buffer = VBUF_BUFFER_START(v);
    viadev_packet_rput_finish *packet = buffer;
    viadev_connection_t *c = (viadev_connection_t *) (s->connection);

    /* fill in the packet */
    PACKET_SET_HEADER_NFR(packet, c, VIADEV_PACKET_RPUT_FINISH);
    packet->rreq = REQ_TO_ID(s->receive_id);

    /* mark MPI send complete when VIA send completes */
    v->shandle = s;

    /* prepare descriptor and post */
    vbuf_init_send(v, sizeof(viadev_packet_rput_finish));
    viadev_post_send(c, v);

}

void viadev_rget_finish(MPIR_RHANDLE * r)
{
    vbuf *v = get_vbuf();
    void *buffer = VBUF_BUFFER_START(v);
    viadev_packet_rget_finish *packet = buffer;
    viadev_connection_t *c = (viadev_connection_t *) (r->connection);

    /* fill in the packet */
    PACKET_SET_HEADER_NFR(packet, c, VIADEV_PACKET_RGET_FINISH);
    packet->sreq = REQ_TO_ID(r->send_id);

    v->shandle = r;

    if(viadev_use_nfr) {
        r->fin = v;
    }
    /* prepare descriptor and post */
    vbuf_init_send(v, sizeof(viadev_packet_rget_finish));
    viadev_post_send(c, v);
}

/* add a function here directly instead of putting it in viutil.c */
static void viutil_spinandwaitcq(struct ibv_wc *sc)
{
    int ret = 1, ne = 0;

    unsigned long long spin_count = 0;

#ifdef ADAPTIVE_RDMA_FAST_PATH
    vbuf *v;
    int ofo;
#endif

    sc->wr_id = 0;

    while (1) {
        /* Chek if it was fatal */
        if (VIADEV_UNLIKELY(1 == viadev_use_nfr && FATAL == nfr_fatal_error)) {
            if (nfr_restart_hca() < 0) {
                error_abort_all(IBV_RETURN_ERR, "Failed restore connection");
            }
            sc->wr_id = 0;
            return;
        }

#ifdef MCST_SUPPORT
        if ((viadev.bcast_info.bcast_called == 1) && (is_mcast_enabled)) {
            if (retransmission == 0) {
                process_acks();

                slide_window();

                check_time_out();

            }
        }
#endif

#ifdef _SMP_
        if ((!disable_shared_mem) && SMP_INIT) {
            if (MPID_SMP_Check_incoming() == MPI_SUCCESS) {
                sc->wr_id = 0;
                ret = 0;
                break;
            }
        }
#endif
        
        /* ON_DEMAND check for new connection */
        odu_test_new_connection();
        
#ifdef ADAPTIVE_RDMA_FAST_PATH
        ret = poll_rdma_buffer((void *) &v, &ofo);
        if (ret == 0) {
            sc->wr_id = (aint_t) v;
            sc->opcode = IBV_WC_RECV;
            break;
        }
#endif
        if(viadev_use_blocking) {

            while (spin_count < viadev_max_spin_count) {

                ne = ibv_poll_cq(viadev.cq_hndl, 1, sc);

                if (ne == 0) {
                    spin_count++;
                } else if (ne < 0) {
                    error_abort_all(IBV_RETURN_ERR,
                            "Got error polling CQ (ibv_poll_cq() returned %d)",
                            ne);
                } else {
                    break;
                }
            }

            if (spin_count == viadev_max_spin_count) {

                /* Okay ... spun long enough, now time to go to sleep! */

                struct ibv_cq *ev_cq;
                void *ev_ctx;

                do {
                    int ret = ibv_get_cq_event(viadev.comp_channel, &ev_cq, &ev_ctx);
                    if (ret && errno != EINTR) {
                        error_abort_all(IBV_RETURN_ERR,
                                "Failed to get cq event (ibv_get_cq_event() returned %d)", ret);
                    }
                } while (ret && errno == EINTR);

                if (ev_cq != viadev.cq_hndl) {
                    error_abort_all(GEN_ASSERT_ERR, "Event in unknown CQ");
                }

                ibv_ack_cq_events(viadev.cq_hndl, 1);

                if (ibv_req_notify_cq(viadev.cq_hndl, 0)) {
                    error_abort_all(IBV_RETURN_ERR,
                            "Couldn't request for CQ notification (ibv_req_notify_cq() returned 0)");
                }

                ne = ibv_poll_cq(viadev.cq_hndl, 1, sc);
            }
        } else {
            ne = ibv_poll_cq(viadev.cq_hndl, 1, sc);
        }

        if (ne > 0) {
            void * vbuf_addr = (void *) ((aint_t) sc->wr_id);
            if (sc->status != IBV_WC_SUCCESS) {
                int dest_rank = ((vbuf *) vbuf_addr)->grank;
                if (!viadev_use_nfr) {
                    error_abort_all(IBV_RETURN_ERR,
                            "Got completion with error %s, "
                            "code=%d, dest [%d:%s]",
                            wc_code_to_str(sc->status), sc->status,
                            dest_rank, viadev.processes[dest_rank]);
                } else {
                    NFR_PRINT("[%d:%s] Got completion with error %s, "
                            "code=%d, dest [%d:%s] qp=%x - Starting Network Recovery process\n",
                            viadev.me, viadev.my_name,
                            wc_code_to_str(sc->status), sc->status,
                            dest_rank, viadev.processes[dest_rank],
                            sc->qp_num);
                    fflush(stderr);
                    if (nfr_process_qp_error(sc) < 0) {
                        error_abort_all(GEN_ASSERT_ERR,
                                        "Failed restore connection to [%d:%s]",
                                        dest_rank, viadev.processes[dest_rank]);
                    }
                    /* reset */
                    sc->wr_id = 0;
                }
            }

            ret = 0;

            break;
        } else if (ne < 0) {
            error_abort_all(IBV_RETURN_ERR,
                            "Got error polling CQ (ibv_poll_cq() returned %d)",
                            ne);
        }

        if (ret == 1)
            continue;
        /* error when comes here */
        error_abort_all(GEN_ASSERT_ERR,
                        "Error ret=%d",
                        ret);
        break;
    }
}

void process_rdma_read_completion(vbuf * v)
{
    MPIR_RHANDLE *rhandle;
    viadev_connection_t *c = NULL;

    viadev_packet_rget *packet =
        (viadev_packet_rget *) VBUF_BUFFER_START(v);

    c = &(viadev.connections[v->grank]);

    /* Since we have a RDMA read completion,
     * we have to make progress on the ones which
     * were queued due to lack of outstanding reads
     */
    c->rdma_reads_avail++;
    if (c->ext_rdma_read_head) {
        viadev_ext_rdma_read_start(c);
    }

    rhandle = (MPIR_RHANDLE *) packet->rreq;

    /* Process the RDMA read completion */
    viadev_rget_finish(rhandle);

    /* Reset some fields for
     * sanity check */
    v->shandle = NULL;
    release_vbuf(v);
}

void viadev_process_r3_ack(vbuf *v, viadev_connection_t *c, 
        viadev_packet_r3_ack *h)
{
    c->pending_r3_data -= h->ack_data;

    assert(c->pending_r3_data >= 0);

    PUSH_FLOWLIST(c);
}

#ifdef MEMORY_RELIABLE
void viadev_process_ack(vbuf * v, viadev_connection_t * c, viadev_packet_crc_ack* h){
    int ack_id;
    vbuf* v1;
    viadev_connection_t *c1 ;

    if (h->ack_type == 0){ /* Ack*/
        ack_id = h->acked_seq_number;
        dequeue_crc_queue(ack_id, c);
    }
    else{       /* Nack */
        ack_id = h->acked_seq_number;
        v1 = search_crc_queue(c, ack_id);
        assert(v1 != NULL);
        c1 = &viadev.connections[v1->grank];

        viadev_post_send(c1, v1);
    }
}
#endif

static void process_srq_limit_event()
{
    int post_new;
    struct ibv_srq_attr srq_attr;


    pthread_spin_lock(&viadev.srq_post_spin_lock);


    /* dynamically re-size the SRQ to be larger */
    viadev_srq_fill_size *= 2;
    if(viadev_srq_fill_size > viadev_srq_alloc_size) {
        viadev_srq_fill_size = viadev_srq_alloc_size;
    } 

    viadev_credit_preserve = (viadev_srq_fill_size > 200) ? 
        (viadev_srq_fill_size - 100) : (viadev_srq_fill_size / 2);

    post_new = viadev.posted_bufs;
    viadev.posted_bufs +=
        viadev_post_srq_buffers(viadev_srq_fill_size -
                viadev_srq_limit);
    post_new = viadev.posted_bufs - post_new;

    pthread_spin_unlock(&viadev.srq_post_spin_lock);

    if(!post_new) {

        pthread_mutex_lock(&viadev.srq_post_mutex_lock);

        viadev.srq_zero_post_counter++;

        while(viadev.srq_zero_post_counter >= 
                viadev_srq_zero_post_max) {

            /* Cannot post to SRQ, since all WQEs
             * might be waiting in CQ to be pulled out */

            pthread_cond_wait(&viadev.srq_post_cond,
                    &viadev.srq_post_mutex_lock);
        }

        pthread_mutex_unlock(&viadev.srq_post_mutex_lock);

    } else {

        /* Was able to post some, so erase old counter */

        if(viadev.srq_zero_post_counter) {
            viadev.srq_zero_post_counter = 0;
        }
    }

    pthread_spin_lock(&viadev.srq_post_spin_lock);

    srq_attr.max_wr = viadev_srq_fill_size;
    srq_attr.max_sge = 1;
    srq_attr.srq_limit = viadev_srq_limit;

    if (ibv_modify_srq(viadev.srq_hndl, &srq_attr, IBV_SRQ_LIMIT)) {
        error_abort_all(GEN_EXIT_ERR,
                "Couldn't modify SRQ limit (%u) after posting %d",
                viadev_srq_limit, post_new);
    }

    pthread_spin_unlock(&viadev.srq_post_spin_lock);
}

void async_thread(void *context)
{
    struct ibv_async_event event;
    int ret;

    /* This thread should be in a cancel enabled state */
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    while (1) {

        do {
            ret = ibv_get_async_event((struct ibv_context *) context, &event);

            if (ret && errno != EINTR) {
                fprintf(stderr, "Error getting event!\n");
            }
        } while(ret && errno == EINTR);

        switch (event.event_type) {
            /* Fatal */
            case IBV_EVENT_CQ_ERR:
            case IBV_EVENT_QP_FATAL:
            case IBV_EVENT_QP_REQ_ERR:
            case IBV_EVENT_QP_ACCESS_ERR:
            case IBV_EVENT_SRQ_ERR:
                break;
                
            case IBV_EVENT_PATH_MIG:
                /* If APM is defined, reload an alternate path as soon
                 * as the one transition is complete */
                if(viadev_use_apm) {
                    reload_alternate_path((&event)->element.qp);
                }
                break;
            case IBV_EVENT_PATH_MIG_ERR:
            case IBV_EVENT_DEVICE_FATAL:
                if (viadev_use_nfr) {
                    /* Fire the fatal flag */
                    nfr_fatal_error = FATAL;
                }
                break;

            case IBV_EVENT_COMM_EST:
            /* the port may return to ACTIVE state after error, so
             * we ignore the port error event */
            case IBV_EVENT_PORT_ERR: 
            /* we may ignore the IBV_EVENT_QP_LAST_WQE_REACHED,
             * poll_cq will show the real error */
            case IBV_EVENT_QP_LAST_WQE_REACHED:
            case IBV_EVENT_PORT_ACTIVE:
            case IBV_EVENT_SQ_DRAINED:
            case IBV_EVENT_LID_CHANGE:
            case IBV_EVENT_PKEY_CHANGE:
            case IBV_EVENT_SM_CHANGE:
            case IBV_EVENT_CLIENT_REREGISTER:
                break;

            case IBV_EVENT_SRQ_LIMIT_REACHED:

                if(viadev_use_srq) {
                    process_srq_limit_event();
                }

                break;
            default:
                fprintf(stderr,
                        "[%d] Got unknown event %d ... continuing ...\n",
                        viadev.me, event.event_type);
        }

        ibv_ack_async_event(&event);
    }
}


int perform_manual_apm(struct ibv_qp* qp)
{
    struct ibv_qp_attr attr;
    struct ibv_qp_init_attr init_attr;
    enum ibv_qp_attr_mask attr_mask;

    static int count_to_apm = 1;

    count_to_apm++;

    if(0 != (count_to_apm % 
                (viadev.np * apm_count))) {
        return 0;
    }

    memset(&attr, 0, sizeof attr);
    memset(&init_attr, 0, sizeof init_attr);
    attr_mask = 0;

    lock_apm();

    if(ibv_query_qp(qp, &attr,
                attr_mask, &init_attr)) {
        error_abort_all(GEN_EXIT_ERR, "Failed to query QP");
    }
    
    if((attr.path_mig_state == IBV_MIG_ARMED)) {
        attr.path_mig_state = IBV_MIG_MIGRATED;
        ibv_modify_qp(qp, &attr,
                    IBV_QP_PATH_MIG_STATE);
    }

    unlock_apm();

    return 0;
}


int reload_alternate_path(struct ibv_qp *qp)
{

    struct ibv_qp_attr attr;
    struct ibv_qp_init_attr init_attr;
    enum ibv_qp_attr_mask attr_mask = IBV_QP_STATE;
    
    lock_apm();
    
    if (ibv_query_qp(qp, &attr,
                attr_mask, &init_attr)) {
        error_abort_all(GEN_EXIT_ERR, "Failed to query QP");
    }   
    
    /* This value should change with enabling of QoS */
    attr.alt_ah_attr.sl =  attr.ah_attr.sl;
    
    attr.alt_timeout = attr.timeout;
    
    attr.path_mig_state = IBV_MIG_REARM;
    
    attr.alt_pkey_index = attr.pkey_index;
    
    attr.alt_ah_attr.static_rate = attr.ah_attr.static_rate;
    
    attr.alt_port_num = attr.port_num;

    attr.alt_ah_attr.src_path_bits =
        (attr.ah_attr.src_path_bits + 1) %
        power_two(viadev.lmc);

    attr.alt_ah_attr.dlid =
        attr.ah_attr.dlid - attr.ah_attr.src_path_bits + 
            attr.alt_ah_attr.src_path_bits;

    attr_mask = 0;

    attr_mask |= IBV_QP_ALT_PATH;
    attr_mask |= IBV_QP_PATH_MIG_STATE;

    if (ibv_modify_qp(qp, &attr, attr_mask)) {
        error_abort_all(GEN_EXIT_ERR, "Failed to modify QP");
    }

    unlock_apm();

    return 0;
}

