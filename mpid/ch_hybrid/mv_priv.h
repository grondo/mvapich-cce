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

#ifndef _MV_PRIV_H
#define _MV_PRIV_H

#include <pthread.h>
#include "mv_param.h"
#include "mv_packet.h"
#include "ibverbs_header.h"
#include "mv_buf.h"
#include "req.h"
#include "mpid.h"
#include "ibverbs_header.h"
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include "mv_priv.h"
#include "process/pmgr_collective_client.h"


#define GEN_EXIT_ERR     -1     /* general error which forces us to abort */
#define GEN_ASSERT_ERR   -2     /* general assert error */
#define IBV_RETURN_ERR   -3     /* ibverbs funtion return error */
#define IBV_STATUS_ERR   -4     /* ibverbs funtion status error */

#define error_abort_all(code, message, args...)  {                  \
        fprintf(stderr, "[%d] Abort: ", mvdev.me);                     \
        fprintf(stderr, message, ##args);                               \
        fprintf(stderr, " at line %d in file %s\n", __LINE__, __FILE__);\
        assert(0); \
        pmgr_abort(code, message, ##args);        \
        exit(code);                                                     \
}

#define MAX_SEQ_NUM 8191
#define WINDOW_ABS_MAX  100

#define UD_HOSTSEQ_ALL  0xffffffff
#define UD_SEQ_BITS 13
#define UD_HOST_BITS 18
#define UD_TYPE_BITS 1

#define UD_TYPE_MASK ~(UD_HOSTSEQ_ALL << UD_TYPE_BITS)
#define UD_SEQ_MASK (UD_HOSTSEQ_ALL << (32 - UD_SEQ_BITS))
#define UD_HOST_MASK ~(UD_TYPE_MASK | UD_SEQ_MASK)

#define CREATE_THS_TUPLE(_type, _host, _seq)     \
(((_type) & UD_TYPE_MASK) |                   \
((_host << UD_TYPE_BITS) & UD_HOST_MASK) |    \
((_seq << (UD_TYPE_BITS + UD_HOST_BITS)) & UD_SEQ_MASK))

#define READ_THS_TUPLE(_tuple, _type, _host, _seq)     \
{ \
    *_type = (_tuple & UD_TYPE_MASK); \
    *_host = (_tuple & UD_HOST_MASK) >> UD_TYPE_BITS; \
    *_seq  = (_tuple & UD_SEQ_MASK) >> (UD_TYPE_BITS + UD_HOST_BITS); \
}

#define INCREMENT_SEQ_NUM(_seqnum) { \
    *(_seqnum) = ((*_seqnum) + 1) % MAX_SEQ_NUM; \
    if(0 == *(_seqnum)) { *(_seqnum) = 1; } \
}

#define GET_NEXT_PKT(_dest) mvdev.connections[_dest].seqnum_next_tosend

#define INC_NEXT_PKT(_dest) { \
    mvdev.connections[_dest].seqnum_next_tosend = (mvdev.connections[_dest].seqnum_next_tosend + 1) % MAX_SEQ_NUM; \
    if(0 == mvdev.connections[_dest].seqnum_next_tosend) { \
        mvdev.connections[_dest].seqnum_next_tosend = 1; \
    } \
}

#define INCL_BETWEEN(_val, _start, _end) \
    (((_start > _end) && (_val >= _start || _val <= _end)) || \
    ((_end > _start) && (_val >= _start && _val <= _end)) ||  \
     ((_end == _start) && (_end == _val)))


#define PEEK_RECV_WIN(_c)  (_c)->recv_window_head->seqnum

#define SEND_WIN_CHECK(_c, _v, _len) {                                                      \
    if(MVDEV_UNLIKELY((c->send_window_segments > mvparams.send_window_segments_max ) ||     \
                (c->ext_window_head != NULL && c->ext_window_head != v))) {                 \
        mvdev_windowq_queue(c, v, _len);                                                    \
        return;                                                                             \
    }                                                                                       \
}                                                                                           

#define SET_INLINE(_desc, _len, _qp) { \
    if((_len) <= (_qp)->max_inline) { \
        (_desc)->sr.send_flags = IBV_SEND_SIGNALED | IBV_SEND_INLINE; \
    } else { \
        (_desc)->sr.send_flags = IBV_SEND_SIGNALED; \
    } \
}

#ifdef XRC
enum {
    MV_XRC_CONN_SHARED_INIT,
    MV_XRC_CONN_SHARED_CONNECTING,
    MV_XRC_CONN_SHARED_ESTABLISHED,
    MV_XRC_CONN_SINGLE_INIT,
    MV_XRC_CONN_SINGLE_SRQN_REQUESTED,
    MV_XRC_CONN_SINGLE_SRQN_RECEIVED,
    MV_XRC_CONN_SINGLE_ESTABLISHED,
};
#endif


enum {
    MV_RELIABLE_NONE,
    MV_RELIABLE_FULL,
    MV_RELIABLE_FULL_X,
    MV_RELIABLE_LOCK_SBUF,
    MV_RELIABLE_FREE_SBUF,
    MV_RELIABLE_INVALID
};

typedef struct _mv_rndv_rput_unrel_tag {
        int tag;
        struct _mv_rndv_rput_unrel_tag * prev;
        struct _mv_rndv_rput_unrel_tag * next;
} mv_rndv_rput_unrel_tag;

typedef struct {
    struct ibv_cq      *send_cq;
    struct ibv_cq      *recv_cq;
    struct ibv_srq     *srq;
    struct ibv_pd      *pd;
    struct ibv_qp_cap  cap;
    uint32_t           sq_psn;
} mv_qp_setup_information;

typedef struct {
    /* app level */
    uint64_t msg_profile[16];
    uint64_t msgv_profile[16];

    /* internal */
    uint64_t ud_msg_type[20][16];
    uint64_t rc_msg_type[20][16];
    uint64_t rcfp_msg_type[20][16];
    uint64_t rcr_msg_type[16];
    uint64_t udz_msg_type[16];
    uint64_t smp_msg_type[16];

    uint64_t udv_msg_type[20][16];
    uint64_t rcv_msg_type[20][16];
    uint64_t rcfpv_msg_type[20][16];
    uint64_t rcrv_msg_type[16];
    uint64_t udzv_msg_type[16];
    uint64_t smpv_msg_type[16];

    uint64_t total_recv_bytes;
    uint64_t total_recvb_bytes;
    uint64_t xrc_conn;
    uint64_t xrc_xconn;

    uint64_t max_send_window_bytes;
    uint64_t send_window_bytes;
    int max_send_entries;
    int send_entries;

    uint64_t control_ack;
    uint64_t control_all;
    uint64_t control_ignore;

} mv_msg_info;

typedef struct poolptr {
    void *next;
    void *prev;
} poolptr;

typedef struct {
    struct ibv_device * device;
    struct ibv_device_attr device_attr;
    struct ibv_pd * pd;
    struct ibv_context * context;
    struct ibv_port_attr * port_attr;
    struct ibv_port_attr * default_port_attr;
    pthread_t async_thread;
} mv_hca;

typedef struct _mv_srq {
    struct ibv_srq * srq;
    int buffer_size;
    int recvq_size;
    int limit;
} mv_srq;

typedef struct _mv_rpool {
    int credit_preserve;
    int credit_preserve_lazy;
    int capacity;
    int posted;
    int buffer_size;

    /* this is for non-SRQ cases */
    int credit_toreturn;

    mv_srq * srq;
    void * qp;

    struct _mv_rpool * next;
} mv_rpool;

/* A new allocation of this structure is required for 
 * each QP that is created... so we may require additional
 * QPs only for some connections and not others
 */

/* TODO: make different mv_qp types for XRC vs. RC_RQ, vs. RC_SRQ */
typedef struct _mv_qp {
    struct ibv_qp *qp;
    uint8_t type;
    void * ch_ptr;

    int send_wqes_avail;
    int send_wqes_total;

    mv_rpool * rpool;
    uint16_t max_inline;
    uint16_t max_send_size;
    uint16_t unsignaled_count;

    mv_sdescriptor *ext_sendq_head;
    mv_sdescriptor *ext_sendq_tail;
    uint16_t ext_sendq_size;

    mv_sdescriptor *ext_backlogq_head;
    mv_sdescriptor *ext_backlogq_tail;

    /* this next field is required for credit-based rqs */
    uint16_t send_credits_remaining;

    mv_hca * hca;
    struct ibv_port_attr * port;
    struct _mv_qp * next;
    uint32_t remote_qpn;
    uint32_t **xrc_srqn;
} mv_qp;

/* only one is required since any QP will be able to send 
 * to it -- so don't connect it to any qp
 */
typedef struct _mvdev_channel_rc_fp_cached {
    int src_lrank;
    int tag;
    int context_id;
} mvdev_channel_rc_fp_cached;

typedef struct _mvdev_channel_rc_fp {
    int buffer_size;

    uint16_t send_head;
    uint16_t send_tail;
    uint16_t recv_head;
    uint16_t recv_tail;

    mvdev_channel_rc_fp_cached cached_send;
    mvdev_channel_rc_fp_cached cached_recv;

    mv_sbuf_region * send_region;
    mv_sbuf * send_sbufs;
    mv_buf_region * send_buf_region;

    mv_rbuf_region * recv_region;
    mv_rbuf * recv_rbufs;
    mv_rbuf * recv_head_rbuf;
    mv_buf_region * recv_buf_region;

    uint32_t send_rkey;
    char * send_rbuffer;
} mvdev_channel_rc_fp;

typedef struct _mvdev_channel_rc {
    mv_qp qp[MAX_NUM_HCAS];
    int last_qp;
    int buffer_size;
    int type;

    struct _mvdev_channel_rc * next;
    struct _mvdev_channel_rc * prev;
} mvdev_channel_rc;

#ifdef XRC
/* shared structure between all connections that 
 * are to the same node
 */
typedef struct _mvdev_channel_xrc_shared {
    mv_qp qp[MAX_NUM_HCAS];
    int last_qp;
    int is_xtra;

    /* this allows us to set of the channel has already
     * been requested or not 
     */
    int status;

    /* protect status, lastqp, and qp data structures */
    pthread_spinlock_t  lock;

    /* we can have more than one connection to a node
     * potentially -- every process should just use one
     * though. Potentially this could be changed later 
     * if there are performance benefits to using
     * multiple QPs
     */
    struct _mvdev_channel_xrc_shared *xtra_xrc_next;

    void * xrc_channel_ptr;
} mvdev_channel_xrc_shared;

/* each connection has this structure */
typedef struct _mvdev_srqn_remote {
    uint32_t buffer_size;
    uint32_t srqn[MAX_NUM_HCAS];

    struct _mvdev_srqn_remote * prev;
    struct _mvdev_srqn_remote * next;
} mvdev_srqn_remote;

typedef struct _mvdev_channel_xrc {
    mvdev_channel_xrc_shared * shared_channel;

    int status;

    /* for each remote process we may have different 
     * buffer sizes available!
     */
    mvdev_srqn_remote * remote_srqn_head;
    mvdev_srqn_remote * remote_srqn_tail;
} mvdev_channel_xrc;


#endif

enum {
    MVDEV_CH_STATUS_NULL = 2,
    MVDEV_CH_STATUS_REQUESTED,
    MVDEV_CH_STATUS_REPLIED,
};

typedef struct _mvdev_channel_request {
    int rank;
    int req_type;
    int status;
    void * target_request_id;
    void * initiator_request_id;
    struct _mvdev_channel_request * next;
    struct _mvdev_channel_request * prev;
} mvdev_channel_request;

typedef struct _mvdev_channel_request_srqn {
    int rank;
    int req_type;
    int status;
    void * target_request_id;
    void * initiator_request_id;
    struct _mvdev_channel_request * next;
    struct _mvdev_channel_request * prev;
    int num_srqs;
    uint32_t srqn[MAX_NUM_HCAS][MAX_SRQS];
    uint32_t buffer_size[MAX_SRQS];
} mvdev_channel_request_srqn;

typedef struct _mvdev_channel_request_rq {
    int rank;
    int req_type;
    int status;
    void * target_request_id;
    void * initiator_request_id;
    struct _mvdev_channel_request * next;
    struct _mvdev_channel_request * prev;
    mvdev_setup_rq setup_rq;
    void * channel_ptr;
} mvdev_channel_request_rq;

#define mvdev_channel_request_xrc mvdev_channel_request_rq

typedef struct _mvdev_channel_request_srq {
    int rank;
    int req_type;
    int status;
    void * target_request_id;
    void * initiator_request_id;
    struct _mvdev_channel_request * next;
    struct _mvdev_channel_request * prev;
    uint8_t num_srqs;
    mvdev_setup_srq setup_srq[MAX_SRQS];
    void * channel_ptr;
} mvdev_channel_request_srq;

#ifdef XRC
typedef struct {
    uint32_t                uniq_hosts;
    int                     me;    
    int                     fd[MAX_NUM_HCAS]; /* file for domain */
    struct ibv_xrc_domain   *xrc_domain[MAX_NUM_HCAS];
    mvdev_channel_xrc_shared  *connections;
} mvdev_xrc_info_t;
#endif

typedef struct _mvdev_connection_t {

    mvdev_channel_rc_fp * channel_rc_fp;
    mvdev_channel_rc * rc_channel_head;
#ifdef XRC
    mvdev_channel_xrc xrc_channel;
#endif

    int global_rank;
    int rc_enabled;
    int xrc_enabled;
    int rcfp_send_enabled;
    int rcfp_recv_enabled;
    uint16_t queued;
    uint16_t rc_largest;

    struct ibv_ah ** data_ud_ah;

    uint64_t total_messages;
    uint64_t total_volume;

    uint64_t rcfp_messages;

    /* These are global sequence numbers that span
     * all channels. Can allow reliability with CRC 
     * checks, etc. Also allowes re-ordering between
     * channels.
     */
    packet_sequence_t seqnum_next_tosend;
    packet_sequence_t seqnum_next_toack;

    mv_rbuf *           recv_window_head;
    mv_rbuf *           recv_window_tail;
    int                 recv_window_size;
    packet_sequence_t   recv_window_start_seq;

    mv_sbuf *           send_window_head;
    mv_sbuf *           send_window_tail;
    int                 send_window_size;
    int                 send_window_segments;

    mv_sbuf *           ext_window_head;
    mv_sbuf *           ext_window_tail;

    int                 rcfp_remote_credit;
    int                 rcrq_remote_credit;
    int                 ud_remote_credit;
    int                 r3_segments;
    int blocked_count;

    MPIR_SHANDLE *shandle_head; /* "queue" of send handles to process */
    MPIR_SHANDLE *shandle_tail;
    MPIR_RHANDLE *rhandle;      /* current eager receive "in progress" */

    struct _mvdev_connection_t *nextflow;

    mvdev_channel_request * channel_request_head;
    mvdev_channel_request * channel_request_tail;

    int inflow;
    uint16_t tag_counter;
    mv_rndv_rput_unrel_tag * unrel_tags_head;

    int last_ah;
    mv_msg_info msg_info;
    int alloc_srq;
    int rcfp_send_enabled_xrc_pending;
#ifdef XRC
    struct _mvdev_connection_t * xrc_channel_ptr;
#endif

    mvdev_packet_rcfp_addr * pending_rcfp;
} mvdev_connection_t;

typedef struct {
    uint32_t seqnum;
    int32_t  associated_qpn;
    int32_t  associated_rank;
    uint16_t index;

    struct ibv_qp * ud_qp;
    struct ibv_cq * ud_cq;

    mv_hca * hca;

    poolptr ptr;
} mv_qp_pool_entry;

typedef struct {
    void  (*post_ud_send)(mvdev_connection_t * c, mv_sbuf * v, int total_len, mv_qp *send_qp, int signalled, int lid);
    void (*post_rc_send)(mvdev_connection_t * c, mv_sbuf * v, int total_len, mv_qp *send_qp);
} mv_func;

typedef struct _mv_srq_shared {
    mv_rpool * pool[MAX_NUM_HCAS];
    struct _mv_srq_shared * next;
} mv_srq_shared;

typedef struct {
    mv_msg_info msg_info;
    mvdev_connection_t ** polling_set;
    uint16_t polling_set_size;

    long last_check;
    int rc_connections;
    int rcfp_connections;
    /* these are only for the basic UD QP that gets
     * setup at the beginning
     */
    uint32_t *lids;
    uint32_t *qpns;

    mv_hca * hca;
    mv_hca * default_hca;
    int num_hcas;

    mvdev_connection_t *connections;
    uint8_t *connection_ack_required;

    int num_ud_qps;
    int last_qp;
    mv_qp * ud_qp;

    int num_cqs;
    struct ibv_cq ** cq;

    uint32_t posted_bufs;
    
    mv_sbuf * unack_queue_head;
    mv_sbuf * unack_queue_tail;

    mv_qp * rndv_qp;
    struct ibv_cq ** rndv_cq;

    /* more general things */
    int np;
    int me;
    int global_id;
    char *my_name;

    uint64_t total_volume;
    uint64_t total_messages;

    /* qp pool */
    mv_qp_pool_entry *rndv_pool_qps;
    mv_qp_pool_entry *rndv_pool_qps_free_head;
    mv_qp_setup_information rndv_si;

    mv_rpool * rpool_srq_list;

#ifdef XRC
    mv_srq_shared * xrc_srqs;
#endif

    char * grh_buf;
    struct ibv_mr * grh_mr[MAX_NUM_HCAS];

    int *pids;                    /* add for totalview */
    char **processes;
    char execname[256];           /* add for totalview */

    uint64_t total_recv_buf;
    uint64_t total_send_buf;

#ifdef XRC
    mvdev_xrc_info_t *xrc_info;
#endif
} mvdev_info_t;


extern mvdev_info_t mvdev;
extern mv_func mvdev_func;
extern uint8_t mvdev_header_size;

extern char nullsbuffer, nullrbuffer;

void mvdev_process_send_afterack(mv_sbuf * v);

/* start the threads that provide various services */
void MV_Start_Threads();

struct ibv_mr* register_memory(int hca_num, void *buf, int len);
int deregister_memory(struct ibv_mr *);

#define REQ_TO_ID(h) ((request_id_t)(h))
#define ID_TO_REQ(id)((union MPIR_HANDLE *)(id))


#define SEND_COMPLETE(s) {                                          \
    s->is_complete = 1;                                             \
    if (s->finish != NULL) s->finish(s);                            \
    if (s->dreg_entry != NULL) {                                    \
        dreg_unregister(s->dreg_entry);                             \
    }                                                               \
    s->dreg_entry = NULL;                                           \
    if (s->ref_count == 0) {                                        \
        switch (s->handle_type) {                                   \
            case MPIR_SEND:                                         \
            {                                                       \
                MPID_SendFree(s);                                   \
                    break;                                          \
            }                                                       \
            case MPIR_PERSISTENT_SEND:                              \
            {                                                       \
                MPID_PSendFree(s);                                  \
                break;                                              \
            }                                                       \
            default:                                                \
                error_abort_all(GEN_EXIT_ERR, "SEND_COMPLETE invalid type\n");    \
        }                                                           \
    }                                                               \
}

#define RECV_COMPLETE(r) {                                          \
    r->is_complete = 1;                                             \
    r->s.MPI_ERROR = MPI_SUCCESS;                                   \
    r->s.count = r->len;                                            \
    if (r->finish != NULL) {                                        \
        r->finish(r);                                               \
    }                                                               \
    if (r->dreg_entry != NULL) {                                    \
        dreg_unregister(r->dreg_entry);                             \
    }                                                               \
    r->dreg_entry = NULL;                                           \
    if (r->ref_count == 0) {                                        \
        switch (r->handle_type) {                                   \
            case MPIR_RECV:                                         \
            {                                                       \
                MPID_RecvFree(r);                                   \
                break;                                              \
            }                                                       \
            case MPIR_PERSISTENT_RECV:                              \
            {                                                       \
                MPID_PRecvFree(r);                                  \
                    break;                                          \
            }                                                       \
            default:                                                \
                error_abort_all(GEN_EXIT_ERR, "RECV_COMPLETE invalid type\n");    \
        }                                                           \
    }                                                               \
}

#define MPID_CommInit(oldcomm,newcomm) MPI_SUCCESS
#define MPID_CommFree(oldcomm) MPI_SUCCESS


#define IBV_POST_SR(v, c, err_string) {                             \
    {                                                               \
        struct ibv_wr *bad_wr;                                      \
        if(ibv_post_send(c->vi, &(v->desc.u.sr), &bad_wr)) {        \
            error_abort_all(IBV_RETURN_ERR, err_string);            \
        }                                                           \
    }                                                               \
}

/* See if the SRQ is full or if we need to post more buffers
 * to it
 */

#define CHECK_RECV_BUFFERS() {                                      \
    if(mvdev.posted_bufs <= mvparams.credit_preserve) {             \
        mvdev.posted_bufs +=                                        \
            mvdev_post_rq_buffers(                                  \
                 mvparams.recvq_size - mvdev.posted_bufs);          \
    }                                                               \
}
#define CHECK_RECV_BUFFERS_LAZY() {                                 \
    if(mvdev.posted_bufs <= mvparams.credit_preserve_lazy) {        \
        mvdev.posted_bufs +=                                        \
            mvdev_post_rq_buffers(                                  \
                 mvparams.recvq_size - mvdev.posted_bufs);          \
    }                                                               \
}


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
    MPIR_SHANDLE *_s = c->shandle_head; \
    c->shandle_head = c->shandle_head->nexthandle;                  \
    _s->nexthandle = NULL; \
        if (NULL == c->shandle_head) {                              \
            c->shandle_tail = NULL;                                 \
        }                                                           \
}


void mvdev_eager_pull(MPIR_RHANDLE * rhandle);
void mvdev_recv_r3(MPIR_RHANDLE * rhandle);
int mvdev_post_rq_buffers(mv_rpool *rp, mv_qp * qp, int num_bufs);
int mvdev_post_srq_buffers(mv_rpool *rp, mv_srq * srq, int num_bufs);
void mvdev_copy_unexpected_handle_to_user_handle(
        MPIR_RHANDLE * rhandle,
        MPIR_RHANDLE * unexpected,
        int *error_code);

void flush_all_messages();
void async_thread(void *context);

void mvdev_incoming_ud_zcopy_finish(mv_rbuf * v, mvdev_connection_t * c,
        mvdev_packet_ud_zcopy_finish * h);
void mvdev_incoming_ud_zcopy_ack(mv_rbuf * v, mvdev_connection_t * c,
        mvdev_packet_ud_zcopy_ack * h);


mv_srq * MV_Create_SRQ(int buffer_size, int recvq_size, int limit);

void mvdev_process_recv_inorder(mvdev_connection_t * c, mv_rbuf * v);


void mvdev_recv_cached_eager(mv_rbuf * v, mvdev_connection_t * c);

void mvdev_incoming_ud_zcopy_ack(mv_rbuf * v, mvdev_connection_t * c, mvdev_packet_ud_zcopy_ack * h);

void mvdev_rendezvous_push_zcopy(MPIR_SHANDLE * s, mvdev_connection_t *c);

void mvdev_incoming_rput_finish(mv_rbuf * v, mvdev_connection_t * c,
                        mvdev_packet_rc_rput_finish * h);

void mvdev_profile();

extern mvdev_connection_t *flowlist;

#define PUSH_FLOWLIST(c) {                                          \
    if (0 == c->inflow) {                                           \
        c->inflow = 1;                                              \
        c->nextflow = flowlist;                                     \
        flowlist = c;                                               \
    }                                                               \
}

#define POP_FLOWLIST() {                                            \
    if (flowlist != NULL) {                                         \
        mvdev_connection_t *_c;                                    \
        _c = flowlist;                                              \
        flowlist = _c->nextflow;                                    \
        _c->inflow = 0;                                             \
        _c->nextflow = NULL;                                        \
    }                                                               \
}

void process_flowlist(void);

/* ----------------------------------- */


int MV_Setup_QPs();
void MV_Generate_Address_Handles();
int MV_UD_Initialize_Connection(mvdev_connection_t * c);
mv_rpool * MV_Create_RPool(int capacity, int limit, int buffer_size, mv_srq *srq, mv_qp *qp);

void mvdev_resend(mv_sbuf *v);
void mvdev_recv_ud_zcopy(MPIR_RHANDLE * rhandle);

static inline int mvdev_eager_ok(int len, mvdev_connection_t * c)
{
    if(0 == c->queued) {
        return 1;
    } else {
        return 0;
    }
}

int MV_Setup_Rndv_QPs();

/* Connection Setup */

void MV_Channel_Incoming_Request(mvdev_connection_t * c, mvdev_packet_conn_pkt * req);
void MV_Channel_Incoming_Reply(mvdev_connection_t * c, mvdev_packet_conn_pkt * reply);
void MV_Channel_Incoming_Ack(mvdev_connection_t * c, mvdev_packet_conn_pkt * req);
void MV_Channel_Send_Request(mvdev_connection_t * c,  mvdev_channel_request * req);

void MV_Channel_Connect_RQ_RC(mvdev_connection_t * c, int buf_size, int credit);
void MV_Channel_Connect_SRQ_RC(mvdev_connection_t * c, int buf_size);
#ifdef XRC
void MV_Channel_Connect_XRC(mvdev_connection_t * c);
void MV_Create_XRC_SRQs();
#endif

mvdev_channel_rc * MV_Setup_RC_RQ(mvdev_connection_t *c, int buf_size, int credit);
mvdev_channel_rc * MV_Setup_RC_SRQ(mvdev_connection_t *c, int buf_size);

void MV_Transition_RC_QP(struct ibv_qp * qp, uint32_t dest_qpn,
        uint16_t dest_lid, uint8_t dest_src_path_bits);
void MV_Transition_UD_QP(mv_qp_setup_information *si, struct ibv_qp * qp);
void MV_Transition_XRC_QP(struct ibv_qp * qp, uint32_t dest_qpn, 
        uint16_t dest_lid, uint8_t dest_src_path_bits);

/* Lower Layer Send Operations */

void MV_Send_UD_Normal(mvdev_connection_t * c, mv_sbuf * v, int total_len, mv_qp *send_qp, int signalled, int lid);
void MV_Send_UD_Imm(mvdev_connection_t * c, mv_sbuf * v, int total_len, mv_qp *send_qp, int signaled, int lid);
void MV_Send_RC_Normal(mvdev_connection_t * c, mv_sbuf * v, int total_len, mv_qp *send_qp);
void MV_Send_RC_Imm(mvdev_connection_t * c, mv_sbuf * v, int total_len, mv_qp *send_qp);
void MV_Send_RCFP_Normal( mvdev_connection_t * c, mv_sbuf * v, int total_len, mv_qp *send_qp);
void MV_Send_RCFP_Cached(mvdev_connection_t * c, mv_sbuf * v, int total_len, mv_qp *qp);
#ifdef XRC
void MV_Send_XRC_Normal(mvdev_connection_t * c, mv_sbuf * v, int total_len);
#endif

/* RNDV */
void MV_Rndv_Push(MPIR_SHANDLE * s);
void MV_Rndv_Receive_Start(mv_rbuf * v, mvdev_connection_t * c,
        mvdev_packet_rendezvous_start * header);
void MV_Rndv_Receive_Reply(mv_rbuf * v, mvdev_connection_t * c,
        mvdev_packet_rendezvous_reply * header);
void MV_Rndv_Send_Reply(MPIR_RHANDLE * rhandle);
void MV_Rndv_Send_Finish(MPIR_SHANDLE * s);
void MV_Rndv_Push_R3(MPIR_SHANDLE * s, mvdev_connection_t *c);
void MV_Rndv_Receive_Start_RPut(MPIR_RHANDLE * rhandle);
void MV_Rndv_Push_RPut(MPIR_SHANDLE * s, mvdev_connection_t *c);
void MV_Rndv_Put(mvdev_connection_t * c, mv_sbuf * v, MPIR_SHANDLE * s,
        void *local_address,
        uint32_t local_memhandle, void *remote_address,
        uint32_t remote_memhandle, int nbytes);
void MV_Rndv_Put_Unreliable_Init(mvdev_connection_t * c, mv_sbuf * v, void *local_address,
        uint32_t lkey, void *remote_address, uint32_t rkey, int len, mv_qp * qp, uint32_t srqn);
void MV_Rndv_Put_Reliable_Init(mv_sbuf * v, void *local_address,
                    uint32_t lkey, void *remote_address,
                    uint32_t rkey, int len, mv_qp * qp, uint32_t srqn);
void MV_Rndv_Receive_R3_Data(mv_rbuf * v, mvdev_connection_t * c,
        mvdev_packet_r3_data * h);
void MV_Rndv_Receive_R3_Data_Next(mv_rbuf * v, mvdev_connection_t * c);


void MV_Rndv_Put_Unreliable_Receive_Finish(mv_rbuf * v, mvdev_connection_t * c,
                mvdev_packet_rc_rput_finish * h, MPIR_RHANDLE * rhandle);
void MV_Rndv_Put_Unreliable_Receive_Ack(mvdev_connection_t * c, mvdev_packet_rput_ack * h);
void MV_Rndv_Put_Unreliable_Send_Ack(mvdev_connection_t * c,
                MPIR_RHANDLE * rhandle);
void MV_Rndv_Put_Unreliable_Receive_Imm(mv_rbuf * v, uint32_t imm_data);

#ifdef XRC
/* XRC */
void MV_XRC_Init(int *allhostids, int nhostids, int global_id, char *my_name);
mvdev_channel_xrc_shared * MV_Setup_XRC_Shared(mvdev_connection_t *c);
#endif


#define SET_FOR_HEADERS(_v) { \
    _v->header = 1; \
    _v->header_ptr = _v->base_ptr + MV_HEADER_OFFSET_SIZE; \
}
#define SET_FOR_IMM(_v) { \
    _v->header = 0; \
    _v->header_ptr = _v->base_ptr; \
}

static inline mv_sbuf *get_sbuf_ud(mvdev_connection_t *c, int length_requested)  {
    mv_sbuf *v;
    /* we are going over UD */
    length_requested += 40;

    if(length_requested <= (mvparams.mtu - 40 - MV_HEADER_OFFSET_SIZE)) {
        /* use headers */
        v = get_mv_sbuf(MIN(length_requested + MV_HEADER_OFFSET_SIZE, mvparams.mtu));
        SET_FOR_HEADERS(v);
    } else {
        v = get_mv_sbuf(length_requested);
        SET_FOR_IMM(v);
    }

    v->transport = MVDEV_TRANSPORT_UD;

    mvdev.total_volume += SBUF_MAX_DATA_SZ(v);
    ++(mvdev.total_messages);

    return v;
}

static inline mv_sbuf *get_sbuf_xrc(mvdev_connection_t *c, int length_requested)  {
    mv_sbuf *v;
    if(c->xrc_enabled) {
        v = get_mv_sbuf(MIN(length_requested + MV_HEADER_OFFSET_SIZE, c->rc_largest));
        SET_FOR_HEADERS(v);

        c->total_volume += SBUF_MAX_DATA_SZ(v);
        ++(c->total_messages);

        v->transport = MVDEV_TRANSPORT_XRC;
    } else {
        MV_ASSERT(0);
    }
    return v;

}

static inline mv_sbuf *get_sbuf_rc(mvdev_connection_t *c, int length_requested)  {
    mv_sbuf *v;
    if(c->rc_enabled) {
        if(length_requested <= (mvparams.rc_buf_size - MV_HEADER_OFFSET_SIZE)) {
            v = get_mv_sbuf(MIN(length_requested + MV_HEADER_OFFSET_SIZE, c->rc_largest));
            SET_FOR_HEADERS(v);
        } else {
            v = get_mv_sbuf(length_requested);
            SET_FOR_IMM(v);
        }

        c->total_volume += SBUF_MAX_DATA_SZ(v);
        ++(c->total_messages);

        v->transport = MVDEV_TRANSPORT_RC;
    } else {
        MV_ASSERT(0);
    }
    return v;

}

#define SEND_RCFP_OK(_c, _len) (c->rcfp_send_enabled &&                     \
        c->channel_rc_fp->send_head != c->channel_rc_fp->send_tail &&       \
        ((c->channel_rc_fp->buffer_size - MV_RCFP_OVERHEAD - 40) >= (_len)))
#define SEND_RC_OK(c, length_requested) \
    (c->rc_enabled && length_requested > mvparams.rc_send_threshold)
#define SEND_XRC_OK(c, length_requested) \
    (c->xrc_enabled && length_requested > mvparams.rc_send_threshold)


static inline mv_sbuf *get_sbuf_control(mvdev_connection_t *c, int length_requested)  {
    mv_sbuf *v;

    /*
    if(c->rc_enabled && length_requested >= mvparams.rc_send_threshold && ((length_requested + MV_HEADER_OFFSET_SIZE)) < c->rc_largest) {
        v = get_mv_sbuf(length_requested + MV_HEADER_OFFSET_SIZE);
        SET_FOR_HEADERS(v);

        c->total_volume += SBUF_MAX_DATA_SZ(v);
        c->total_messages++;

        v->transport = MVDEV_TRANSPORT_RC;
    }  else */ {
        /* we are going over UD */
        length_requested += 40;

        if(length_requested <= (mvparams.mtu - 40 - MV_HEADER_OFFSET_SIZE)) {
            /* use headers */
            v = get_mv_sbuf(length_requested + MV_HEADER_OFFSET_SIZE);
            SET_FOR_HEADERS(v);
        } else {
            v = get_mv_sbuf(length_requested);
            SET_FOR_IMM(v);
        }

        v->transport = MVDEV_TRANSPORT_UD;

        if(length_requested > mvparams.rc_setup_threshold) {
            ++(c->total_messages);
        }

    }
    return v;

}

static inline mv_sbuf *get_sbuf(mvdev_connection_t *c, int length_requested)  {
    mv_sbuf *v;

    if(SEND_RCFP_OK(c, length_requested)) {
        v = &(c->channel_rc_fp->send_region->array[c->channel_rc_fp->send_head]);
        if(++(c->channel_rc_fp->send_head) >= viadev_num_rdma_buffer) {
            c->channel_rc_fp->send_head = 0;
        }
        MV_ASSERT(v->transport == MVDEV_TRANSPORT_RCFP);

    } else if(SEND_XRC_OK(c, length_requested)) {

        v = get_mv_sbuf(MIN(length_requested + MV_HEADER_OFFSET_SIZE, 
                    c->rc_largest));
        SET_FOR_HEADERS(v);
        v->transport = MVDEV_TRANSPORT_XRC;

    } else if(SEND_RC_OK(c, length_requested)) {
        if(MVDEV_LIKELY(length_requested <= (c->rc_largest - MV_HEADER_OFFSET_SIZE))) {
            v = get_mv_sbuf(MIN(length_requested + MV_HEADER_OFFSET_SIZE, 
                        c->rc_largest));
            SET_FOR_HEADERS(v);
        } else {
            v = get_mv_sbuf(length_requested);
            SET_FOR_IMM(v);
        }
        v->transport = MVDEV_TRANSPORT_RC;

        if(MVDEV_CH_RC_MULT_SRQ == mvparams.conn_type && c->rc_largest != 8192) {
                    MV_Channel_Connect_SRQ_RC(c, 128);
                    MV_Channel_Connect_SRQ_RC(c, 256);
                    MV_Channel_Connect_SRQ_RC(c, 512);
                    MV_Channel_Connect_SRQ_RC(c, 1024); 
                    MV_Channel_Connect_SRQ_RC(c, 2048);
                    MV_Channel_Connect_SRQ_RC(c, 4096);
                    MV_Channel_Connect_SRQ_RC(c, 8192);
        }
    } else {
        /* we are going over UD */
        length_requested += 40;

        if(MVDEV_LIKELY(length_requested <= (mvparams.mtu - 40 - MV_HEADER_OFFSET_SIZE))) {
            v = get_mv_sbuf(MIN(length_requested + MV_HEADER_OFFSET_SIZE, mvparams.mtu));
            SET_FOR_HEADERS(v);
        } else {
            v = get_mv_sbuf(length_requested);
            SET_FOR_IMM(v);
        }

        v->transport = MVDEV_TRANSPORT_UD;
        c->total_volume += SBUF_MAX_DATA_SZ(v);

        if(length_requested > mvparams.rc_setup_threshold) {
            ++(c->total_messages);
        }

        if(MVDEV_UNLIKELY(!c->xrc_enabled && !c->rc_enabled &&
                c->total_messages > mvparams.rc_threshold 
                && mvdev.rc_connections < mvparams.max_rc_connections)) {
            switch(mvparams.conn_type) {
                case MVDEV_CH_RC_SRQ:
                    MV_Channel_Connect_SRQ_RC(c, mvparams.rc_buf_size); 
                    break;
                case MVDEV_CH_RC_MULT_SRQ:
                    MV_Channel_Connect_SRQ_RC(c, 128);
                    MV_Channel_Connect_SRQ_RC(c, 256);
                    MV_Channel_Connect_SRQ_RC(c, 512);
                    MV_Channel_Connect_SRQ_RC(c, 1024); 
                    MV_Channel_Connect_SRQ_RC(c, 2048);
                    MV_Channel_Connect_SRQ_RC(c, 4096);
                    MV_Channel_Connect_SRQ_RC(c, 8192);
                    break;
                case MVDEV_CH_RC_RQ:
                    MV_Channel_Connect_RQ_RC(c, mvparams.rc_buf_size, mvparams.rc_credits); 
                    break;
#ifdef XRC
                case MVDEV_CH_XRC_SHARED_MULT_SRQ:
                    MV_Channel_Connect_XRC(c);
                    break;
#endif
            }
        }
    }

    return v;
}

void MV_Recv_RC_FP_Addr(mvdev_connection_t * c, mvdev_packet_rcfp_addr * p);
void MV_Setup_RC_FP(mvdev_connection_t * c);
void mvdev_profile();

#ifdef MV_PROFILE
void mvdev_init_connection_profile(mvdev_connection_t *c);
#endif

mv_srq * MV_Get_SRQ(int buf_size);

static inline mv_buf * MV_Copy_Unexpected_Rbuf(mv_rbuf * unexp) {
    /* no need to save header, etc -- so just keep the data part */
    mv_buf * v = get_mv_buf(unexp->byte_len_data);

    if(MVDEV_LIKELY(unexp->byte_len_data > 0)) {
        memcpy(v->ptr, unexp->data_ptr, unexp->byte_len_data);
    }

    v->byte_len = unexp->byte_len_data;

    return v;
}
#define ACK_CREDIT_CHECK(_c, _v) {                                     \
    if(MVDEV_TRANSPORT_UD == _v->transport) {                          \
        if(++(_c->ud_remote_credit) > mvparams.send_ackafter) {        \
            mvdev_explicit_ack(_c->global_rank);                       \
        }                                                              \
    }                                                                  \
    else if(MVDEV_TRANSPORT_RCFP == _v->transport) {                   \
        if(++(_c->rcfp_remote_credit) > 16) {        \
            mvdev_explicit_ack(_c->global_rank);                       \
        }                                                              \
    } else if(_v->transport == MVDEV_TRANSPORT_XRC) { \
        if(++(_c->rcrq_remote_credit) > 10) {        \
            mvdev_explicit_ack(_c->global_rank);                       \
        }                                                              \
    } else if(_v->transport == MVDEV_TRANSPORT_RC) { \
        if(++(_c->rcrq_remote_credit) > 10) {        \
            mvdev_explicit_ack(_c->global_rank);                       \
        }                                                              \
    }                                                                  \
}


#define CHECK_RPOOL(rp)                                                                 \
{                                                                                       \
    if(rp->posted < rp->credit_preserve) {                                              \
        if(rp->srq) {                                                                   \
            rp->posted += mvdev_post_srq_buffers(rp, rp->srq, rp->capacity - rp->posted);   \
        } else {                                                                        \
            rp->posted += mvdev_post_rq_buffers(rp, (mv_qp *) rp->qp, rp->capacity - rp->posted);     \
        }                                                                               \
    }                                                                                   \
}


#define DECR_RPOOL(rp)                                                                  \
{                                                                                       \
    (rp->posted)--;                                                                     \
}

#define SEND_UD_SR(_qp, _v) {                                                     \
    if(MVDEV_UNLIKELY(_qp->send_wqes_avail <= 0 || (NULL != _qp->ext_sendq_head))) {        \
        mvdev_ext_sendq_queue(_qp, &(_v->desc));                                  \
    } else {                                                                                \
        struct ibv_send_wr *bad_wr;                                                         \
        _qp->send_wqes_avail--;                                                             \
        if(MVDEV_UNLIKELY(ibv_post_send(_qp->qp, &(_v->desc.sr), &bad_wr))) {     \
            error_abort_all(IBV_RETURN_ERR,"Error posting to UD QP %d\n",                   \
                    _qp->send_wqes_avail);                                                  \
        }                                                                                   \
    }                                                                                       \
}

#define SEND_UD_SR_CHUNK(_qp, _v, _seg) {                                                   \
    if(MVDEV_UNLIKELY(_qp->send_wqes_avail <= 0 || (NULL != _qp->ext_sendq_head))) {        \
        mvdev_ext_sendq_queue(_qp, &(_v->desc_chunk->desc[_seg]));                          \
    } else {                                                                                \
        struct ibv_send_wr *bad_wr;                                                         \
        _qp->send_wqes_avail--;                                                             \
        if(MVDEV_UNLIKELY(ibv_post_send(_qp->qp, &(_v->desc_chunk->desc[_seg].sr), &bad_wr))) {     \
            error_abort_all(IBV_RETURN_ERR,"Error posting to UD QP %d\n",                   \
                    _qp->send_wqes_avail);                                                  \
        }                                                                                   \
    }                                                                                       \
}

#define SEND_RC_SR_CHUNK(_qp, _v, _segment, _num) {                                                     \
    if(MVDEV_UNLIKELY((_qp->send_wqes_avail - _num) < 0 || (NULL != _qp->ext_sendq_head))) {        \
        mvdev_ext_sendq_queue(_qp, &(_v->desc_chunk->desc[_segment]));                     \
    } else {                                                                                \
        struct ibv_send_wr *bad_wr;                                                         \
        _qp->send_wqes_avail -= _num;                                                       \
        D_PRINT("sending RC (%d)\n", _qp->send_wqes_avail); \
        if(MVDEV_UNLIKELY(ibv_post_send(_qp->qp, &(_v->desc_chunk->desc[_segment].sr), &bad_wr))) {     \
            error_abort_all(IBV_RETURN_ERR,"Error posting to RC QP %d\n",                   \
                    _qp->send_wqes_avail);                                                  \
        }                                                                                   \
    }                                                                                       \
}

#define SEND_RC_SR(_qp, _v) {                                                     \
    if(MVDEV_UNLIKELY(_qp->send_wqes_avail <= 0 || (NULL != _qp->ext_sendq_head))) {        \
        mvdev_ext_sendq_queue(_qp, &(_v->desc));                                  \
    } else {                                                                                \
        struct ibv_send_wr *bad_wr;                                                         \
        _qp->send_wqes_avail--;                                                             \
        if(MVDEV_UNLIKELY(ibv_post_send(_qp->qp, &(_v->desc.sr), &bad_wr))) {     \
            error_abort_all(IBV_RETURN_ERR,"Error posting to RC QP %d\n",                   \
                    _qp->send_wqes_avail);                                                  \
        }                                                                                   \
    }                                                                                       \
}

#define GET_MSG_INDEX(_msg_size, _i) { \
    if(_msg_size < 64) {                                \
        *_i = 0;                                         \
    } else if(_msg_size < 128) {                        \
        *_i = 1;                                         \
    } else if(_msg_size < 256) {                        \
        *_i = 2;                                         \
    } else if(_msg_size < 512) {                        \
        *_i = 3;                                         \
    } else if(_msg_size < 1024) {                       \
        *_i = 4;                                         \
    } else if(_msg_size < 2048) {                       \
        *_i = 5;                                         \
    } else if(_msg_size < 4096) {                       \
        *_i = 6;                                         \
    } else if(_msg_size < 8192) {                       \
        *_i = 7;                                         \
    } else if(_msg_size < 16384) {                      \
        *_i = 8;                                         \
    } else if(_msg_size < 32768) {                      \
        *_i = 9;                                         \
    } else if(_msg_size < 65536) {                      \
        *_i = 10;                                        \
    } else if(_msg_size < 131072) {                     \
        *_i = 11;                                        \
    } else if(_msg_size < 262144) {                     \
        *_i = 12;                                        \
    } else if(_msg_size < 524288) {                     \
        *_i = 13;                                        \
    } else if(_msg_size < 1048576) {                    \
        *_i = 14;                                        \
    } else {                                            \
        *_i = 15;                                        \
    }                                                   \
}

#ifdef MV_PROFILE
#define COLLECT_UD_INTERNAL_MSG_INFO(_msg_size, _type, _c) {    \
    int _i;                                              \
    GET_MSG_INDEX(_msg_size, (&_i)); \
    (_c)->msg_info.ud_msg_type[_type][_i]++;               \
    (_c)->msg_info.udv_msg_type[_type][_i] += _msg_size;               \
}
#define COLLECT_UDZ_INTERNAL_MSG_INFO(_msg_size, _c) {    \
    int _i;                                              \
    GET_MSG_INDEX(_msg_size, (&_i)); \
    (_c)->msg_info.udz_msg_type[_i]++;               \
    (_c)->msg_info.udzv_msg_type[_i] += _msg_size;               \
}
#define COLLECT_RCR_INTERNAL_MSG_INFO(_msg_size, _c) {    \
    int _i;                                              \
    GET_MSG_INDEX(_msg_size, (&_i)); \
    (_c)->msg_info.rcr_msg_type[_i]++;               \
    (_c)->msg_info.rcrv_msg_type[_i] += _msg_size;               \
}
#define COLLECT_RC_INTERNAL_MSG_INFO(_msg_size, _type, _c) {    \
    int _i;                                              \
    GET_MSG_INDEX(_msg_size, (&_i)); \
    (_c)->msg_info.rc_msg_type[_type][_i]++;               \
    (_c)->msg_info.rcv_msg_type[_type][_i] += _msg_size;               \
}
#define COLLECT_RCFP_INTERNAL_MSG_INFO(_msg_size, _type, _c) {    \
    int _i;                                              \
    GET_MSG_INDEX(_msg_size, (&_i)); \
    (_c)->msg_info.rcfp_msg_type[_type][_i]++;               \
    (_c)->msg_info.rcfpv_msg_type[_type][_i] += _msg_size;               \
}
#define COLLECT_SMP_INTERNAL_MSG_INFO(_msg_size, _c) {    \
    int _i;                                              \
    GET_MSG_INDEX(_msg_size, (&_i)); \
    (_c)->msg_info.smp_msg_type[_i]++;               \
    (_c)->msg_info.smpv_msg_type[_i] += _msg_size;               \
}
#define COLLECT_MPI_MSG_INFO(_msg_size, _c) {               \
    int _i;                                             \
    GET_MSG_INDEX(_msg_size, (&_i)); \
    (_c)->msg_info.msg_profile[_i]++;                   \
    (_c)->msg_info.msgv_profile[_i] += _msg_size;               \
}
#else
#define COLLECT_UD_INTERNAL_MSG_INFO(_msg_size, _type, _c)
#define COLLECT_UDZ_INTERNAL_MSG_INFO(_msg_size, _c)
#define COLLECT_RCR_INTERNAL_MSG_INFO(_msg_size, _c)
#define COLLECT_RC_INTERNAL_MSG_INFO(_msg_size, _type, _c)
#define COLLECT_RCFP_INTERNAL_MSG_INFO(_msg_size, _type, _c)
#define COLLECT_MPI_MSG_INFO(_msg_size, _c)
#define COLLECT_SMP_INTERNAL_MSG_INFO(_msg_size, _c)
#endif





#endif /* _MV_PRIV_H */
