
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

#include "cm.h"
#include "viaparam.h"
#include "viapriv.h"
#include "cm_user.h"
#include "ib_init.h"
#include "mpid.h"
#include <errno.h>

typedef enum CM_conn_state_cli {
    CM_CONN_STATE_C_IDLE,
    CM_CONN_STATE_C_REQUESTING,
} CM_conn_state_cli;

typedef enum CM_conn_state_srv {
    CM_CONN_STATE_S_IDLE,
    CM_CONN_STATE_S_REQUESTED,
} CM_conn_state_srv;

#define CM_MSG_TYPE_REQ     0
#define CM_MSG_TYPE_REP     1
#define CM_MSG_TYPE_FIN_SELF  99

typedef struct cm_msg {
    uint32_t req_id;
    uint32_t server_rank;
    uint32_t client_rank;
    uint8_t msg_type;
    lgid lgid;
    uint32_t qpn;
    uint32_t ud_qpn;   /* NR - New UD qp number */
    uint16_t cid;      /* NR - Connection ID. On each reconnect/connect we will increase it */
}cm_msg;

#define DEFAULT_CM_MSG_RECV_BUFFER_SIZE   1024
#define DEFAULT_CM_SEND_DEPTH             10
#define DEFAULT_CM_MAX_SPIN_COUNT         5000   
#define DEFAULT_CM_THREAD_STACKSIZE   (1024*1024)

/*In microseconds*/
#define CM_DEFAULT_TIMEOUT      500000
#define CM_MIN_TIMEOUT           20000

#define CM_UD_DEFAULT_PSN   0

#define CM_UD_SEND_WR_ID  11
#define CM_UD_RECV_WR_ID  13

static int cm_send_depth;
static int cm_recv_buffer_size;
static int cm_ud_psn;
static int cm_req_id_global;
static int cm_max_spin_count;
static pthread_t cm_comp_thread, cm_timer_thread;
static pthread_mutex_t cm_conn_state_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cm_cond_new_pending = PTHREAD_COND_INITIALIZER;
struct timespec cm_timeout;
long cm_timeout_usec;
size_t cm_thread_stacksize;

MPICM_ib_qp_attr cm_ib_qp_attr;

MPICM_conn_state *cm_conn_state;  /*conntion type to all peers */
CM_conn_state_cli *cm_state_cli;        /*connecting states as client to all peers */
CM_conn_state_srv *cm_state_srv;        /*connecting states as server to all peers */
uint16_t *cid; /* Connection ID for all peers */

struct ibv_comp_channel *cm_ud_comp_ch;
struct ibv_qp *cm_ud_qp;
struct ibv_cq *cm_ud_recv_cq;
struct ibv_cq *cm_ud_send_cq;
struct ibv_mr *cm_ud_mr;
struct ibv_ah **cm_ah;          /*Array of address handles of peers */
uint32_t *cm_ud_qpn;            /*Array of ud pqn of peers */

lgid *cm_lgid;               /*Array of lid/gid of all procs */

void *cm_ud_buf;
void *cm_ud_send_buf;           /*length is set to 1 */
void *cm_ud_recv_buf;
int cm_ud_recv_buf_index;
static int page_size;

int MPICM_Lock()
{
    pthread_mutex_lock(&cm_conn_state_lock);
    return 0;
}

int MPICM_Unlock()
{
    pthread_mutex_unlock(&cm_conn_state_lock);
    return 0;
}

int MPICM_Init_lock()
{
    pthread_mutex_init(&cm_conn_state_lock, NULL);
}

int cm_post_ud_recv(void *buf, int size)
{
    struct ibv_sge list;
    struct ibv_recv_wr wr;
    struct ibv_recv_wr *bad_wr;

    memset(&list, 0, sizeof(struct ibv_sge));
    list.addr = (uintptr_t) buf;
    list.length = size + 40;
    list.lkey = cm_ud_mr->lkey;
    memset(&wr, 0, sizeof(struct ibv_recv_wr));
    wr.next = NULL;
    wr.wr_id = CM_UD_RECV_WR_ID;
    wr.sg_list = &list;
    wr.num_sge = 1;

    return ibv_post_recv(cm_ud_qp, &wr, &bad_wr);
}

#ifdef XRC
xrc_hash_t *xrc_hash[XRC_HASH_SIZE];

void cm_activate_xrc_qp_reuse (int peer_rank)
{
    odu_enable_qp(peer_rank, viadev.qp_hndl[peer_rank]);
    cm_state_cli[peer_rank] = CM_CONN_STATE_C_IDLE;
    cm_conn_state[peer_rank] = MPICM_IB_RC_PT2PT;
    viadev.cm_new_connection = 1;
}

int compute_xrc_hash (uint32_t v)
{
    uint8_t *p = (uint8_t *)  &v;
    return ((p[0] ^ p[1] ^ p[2] ^ p[3])  & XRC_HASH_MASK);
}

void clear_xrc_hash (void)
{
    int i;
    xrc_hash_t *iter, *next;
    for (i = 0; i < XRC_HASH_SIZE; i ++) {
        iter = xrc_hash[i];
        while (iter) {
            next = iter->next;
            free (iter);
            iter = next;
        }
    }

}

void add_qp_xrc_hash (int peer, struct ibv_qp *qp)
{
    int hash = compute_xrc_hash (viadev.hostids [peer]);

    xrc_hash_t *iter, *node = (xrc_hash_t *) malloc (xrc_hash_s);
    memset (node, 0, xrc_hash_s);
    node->qp = qp;
    node->host = viadev.hostids [peer];
    node->xrc_qp_dst = peer;

    if (NULL == xrc_hash[hash]) {
        xrc_hash[hash] = node;
        return;
    }

    iter = xrc_hash[hash];
    
    while (iter->next != NULL) {
        iter = iter->next;
    }
    iter->next = node;
}
#endif

struct ibv_qp *cm_create_rc_qp(int rank, int *create, int force)
{
    struct ibv_qp *qp;
    struct ibv_qp_init_attr init_attr;
    struct ibv_qp_attr attr;
    int rc;

    viadev_connection_t * c = &(viadev.connections[rank]);

    memcpy(&init_attr, &(cm_ib_qp_attr.rc_qp_init_attr),
           sizeof(struct ibv_qp_init_attr));

    if(viadev.num_connections > viadev_no_inline_threshold) {
        c->max_inline = init_attr.cap.max_inline_data = 0;
    } else {
        c->max_inline = init_attr.cap.max_inline_data = viadev_max_inline_size;
    }

#ifdef XRC 
    if (viadev_use_xrc) {
        if (!force) {
            int hash = compute_xrc_hash (viadev.hostids[rank]);
            xrc_hash_t *iter = xrc_hash[hash];

            /* Check if we have a RC QP */
            while (iter) {
                if (iter->host == viadev.hostids[rank]) {
                    if (viadev_multihca && viadev.def_hcas[rank] != 
                            viadev.def_hcas[iter->xrc_qp_dst]) {
                        /* We have a connection to a different HCA */
                        iter = iter->next;
                        continue;
                    }
                    viadev.connections[rank].xrc_flags |= 
                        XRC_FLAG_INDIRECT_CONN;
                    viadev.connections[rank].xrc_qp_dst = iter->xrc_qp_dst;
                    *create = XRC_QP_REUSE;
                    return iter->qp;
                }
                iter = iter->next;
            }
        } 
        /* We don't have a qp to the destinatin node */
        init_attr.xrc_domain = viadev.xrc_info->xrc_domain;
        init_attr.qp_type = IBV_QPT_XRC;
        init_attr.srq = NULL;
    }
#endif

    /* incremement the number of connections */
    viadev.num_connections++;

    qp = ibv_create_qp(viadev.ptag, &init_attr);
    if (!qp) {
        CM_ERR("Couldn't create RC QP");
        return NULL;
    }

    memcpy(&attr, &(cm_ib_qp_attr.rc_qp_attr_to_init),
           sizeof(struct ibv_qp_attr));

#ifdef XRC
    if (viadev_use_xrc) {
        attr.qp_state = IBV_QPS_INIT;
        attr.pkey_index = viadev_default_pkey;
        attr.port_num = viadev_default_port;
        attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | 
            IBV_ACCESS_REMOTE_READ;
    }
#endif

    if ((rc = ibv_modify_qp(qp, &attr, cm_ib_qp_attr.rc_qp_mask_to_init))) {
        CM_ERR("Failed to modify RC QP to INIT: rc=%d",rc);
        return NULL;
    }

#ifdef XRC
    if (viadev_use_xrc) {
        viadev.connections[rank].xrc_flags |= XRC_FLAG_DIRECT_CONN;
        viadev.connections[rank].xrc_qp_dst = rank;
    }
#endif

    *create = XRC_QP_NEW;
    return qp;
}

int cm_post_ud_packet(cm_msg * msg)
{
    int peer;
    int ret;
    struct ibv_sge list;
    struct ibv_send_wr wr;
    struct ibv_send_wr *bad_wr;

    struct ibv_wc wc;
    int ne;

    CM_DBG("cm_post_ud_packet Enter");
    
    if (msg->msg_type == CM_MSG_TYPE_REP) {
        peer = msg->client_rank;
    } else {
        peer = msg->server_rank;
    }

    memcpy(cm_ud_send_buf + 40, msg, sizeof(cm_msg));
    memset(&list, 0, sizeof(struct ibv_sge));
    list.addr = (uintptr_t) cm_ud_send_buf + 40;
    list.length = sizeof(cm_msg);
    list.lkey = cm_ud_mr->lkey;

    memset(&wr, 0, sizeof(struct ibv_send_wr));
    wr.wr_id = CM_UD_SEND_WR_ID;
    wr.sg_list = &list;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_SEND;
    wr.send_flags = IBV_SEND_SIGNALED | IBV_SEND_SOLICITED;
    wr.wr.ud.ah = cm_ah[peer];
    wr.wr.ud.remote_qpn = cm_ud_qpn[peer];
    wr.wr.ud.remote_qkey = 0;

    ret = ibv_post_send(cm_ud_qp, &wr, &bad_wr);
    if (ret) {
        CM_ERR("ibv_post_send to ud qp failed: ret=%d",ret);
    }

    /*poll for completion */
    CM_DBG("cm_post_ud_packet Poll");
    
    while (1) {
        ne = ibv_poll_cq(cm_ud_send_cq, 1, &wc);
        if (ne < 0) {
            CM_ERR("poll CQ failed %d", ne);
            return -1;
        } else if (ne == 0)
            continue;

        if (wc.status != IBV_WC_SUCCESS) {
            CM_ERR("Failed status %d for wr_id %d",
                    wc.status, (int) wc.wr_id);
            return -1;
        }

        if (wc.wr_id == CM_UD_SEND_WR_ID) {
            break;
        } else {
            CM_ERR("unexpected completion, wr_id: %d",
                    (int) wc.wr_id);
            return -1;
        }
    }

    CM_DBG("cm_post_ud_packet Exit");

    return 0;
}

/*move qp to rts*/

int cm_enable_qp_init_to_rts(int peer, struct ibv_qp *qp,
                             lgid dlgid, int dqpn)
{
    struct ibv_qp_attr attr;
    int rc;
    CM_DBG("cm_enable_qp_init_to_rts Enter");
    
    memcpy(&attr, &(cm_ib_qp_attr.rc_qp_attr_to_rtr),
           sizeof(struct ibv_qp_attr));
    attr.dest_qp_num = dqpn;

    if (!disable_lmc) {
        attr.ah_attr.dlid = dlgid.lid + (viadev.me + peer) % (power_two(viadev.lmc));
        attr.ah_attr.src_path_bits =
            viadev_default_src_path_bits + 
            (viadev.me + peer) % (power_two(viadev.lmc));
    } else {
        if (viadev_eth_over_ib){
            attr.ah_attr.grh.dgid = dlgid.gid;
        } else {
            attr.ah_attr.dlid = dlgid.lid;
        }
        attr.ah_attr.src_path_bits =
            viadev_default_src_path_bits;
    }
    
    if ((rc = ibv_modify_qp(qp, &attr, cm_ib_qp_attr.rc_qp_mask_to_rtr))) {
        CM_ERR("Failed to modify QP to RTR: rc=%d",rc);
        return -1;
    }

    memcpy(&attr, &(cm_ib_qp_attr.rc_qp_attr_to_rts),
           sizeof(struct ibv_qp_attr));

    if ((rc = ibv_modify_qp(qp, &attr, cm_ib_qp_attr.rc_qp_mask_to_rts))) {
        CM_ERR("Failed to modify QP to RTS: rc=%d", rc);
        return -1;
    }

    CM_DBG("cm_enable_qp_init_to_rts Exit");
   
    if(viadev_use_apm) {
        reload_alternate_path(qp);
    } 

    return 0;
}

/*move qp to rtr*/

int cm_enable_qp_init_to_rtr(int peer, struct ibv_qp *qp,
                             lgid dlgid, int dqpn)
{
    struct ibv_qp_attr attr;
	int rc;
	

    CM_DBG("cm_enable_qp_init_to_rtr Enter");
        
    memcpy(&attr, &(cm_ib_qp_attr.rc_qp_attr_to_rtr),
           sizeof(struct ibv_qp_attr));

    attr.dest_qp_num = dqpn;

    if (!disable_lmc) {
        attr.ah_attr.dlid = dlgid.lid + (viadev.me + peer) % (power_two(viadev.lmc));
        attr.ah_attr.src_path_bits =
            viadev_default_src_path_bits + 
            (viadev.me + peer) % (power_two(viadev.lmc));
    } else {
        if (viadev_eth_over_ib){
            attr.ah_attr.grh.dgid = dlgid.gid;
            /* fprintf(stdout, "[%d] Setting remote MAC %012llx\n", viadev.me, viadev.mac_table[i]); 
               fflush(stdout);
               */
        } else {
            attr.ah_attr.dlid = dlgid.lid;
        }
        attr.ah_attr.src_path_bits =
            viadev_default_src_path_bits;
    }

    if ((rc = ibv_modify_qp(qp, &attr, cm_ib_qp_attr.rc_qp_mask_to_rtr))) {
        CM_ERR("Failed to modify QP to RTR: rc=%d", rc);
        return -1;
    }

    CM_DBG("cm_enable_qp_init_to_rtr Exit");
        
    return 0;
}

/*Just modify to rts, not register anything*/

int cm_enable_qp_rtr_to_rts(struct ibv_qp *qp)
{
    struct ibv_qp_attr attr;
    int rc;
    CM_DBG("cm_enable_qp_rtr_to_rts Enter");
    
    memcpy(&attr, &(cm_ib_qp_attr.rc_qp_attr_to_rts),
           sizeof(struct ibv_qp_attr));

    if ((rc = ibv_modify_qp(qp, &attr, cm_ib_qp_attr.rc_qp_mask_to_rts))) {
        CM_ERR("Failed to modify QP to RTS: rc=%d",rc);
        return -1;
    }
    CM_DBG("cm_enable_qp_rtr_to_rts Exit");
    
    return 0;
}

typedef struct cm_packet {
    struct timeval timestamp;        /*the time when timer begins */
    cm_msg payload;
} cm_packet;

typedef struct cm_pending {
    int cli_or_srv;             /*pending as a client or server */
    int peer;                   /*if peer = self rank, it's head node */
    cm_packet *packet;
    struct cm_pending *next;
    struct cm_pending *prev;
} cm_pending;

int cm_pending_num;

#define CM_PENDING_SERVER   0
#define CM_PENDING_CLIENT   1

cm_pending *cm_pending_head = NULL;

cm_pending *cm_pending_create()
{
    CM_DBG("cm_pending_create Enter");
    cm_pending *temp = (cm_pending *) malloc(sizeof(cm_pending));
    memset(temp, 0, sizeof(cm_pending));
    CM_DBG("cm_pending_create Exit");
    return temp;
}

int cm_pending_init(cm_pending * pending, cm_msg * msg)
{
    CM_DBG("cm_pending_init Enter");
    
    if (msg->msg_type == CM_MSG_TYPE_REQ) {
        pending->cli_or_srv = CM_PENDING_CLIENT;
        pending->peer = msg->server_rank;
    } else if (msg->msg_type == CM_MSG_TYPE_REP) {
        pending->cli_or_srv = CM_PENDING_SERVER;
        pending->peer = msg->client_rank;
    } else {
        CM_ERR("error message type unknown. type=%d",msg->msg_type);
        return -1;
    }
    pending->packet = (cm_packet *) malloc(sizeof(cm_packet));
    memcpy(&(pending->packet->payload), msg, sizeof(cm_msg));
    CM_DBG("cm_pending_init Exit");
    
    return 0;
}

cm_pending *cm_pending_search_peer(int peer, int cli_or_srv)
{
    cm_pending *pending = cm_pending_head;
    CM_DBG("cm_pending_search_peer Enter");
    
    while (pending->next != cm_pending_head) {
        pending = pending->next;
        if (pending->cli_or_srv == cli_or_srv && pending->peer == peer) {
            return pending;
        }
    }
    CM_DBG("cm_pending_search_peer Exit");
     
    return NULL;
}

int cm_pending_append(cm_pending * node)
{
    CM_DBG("cm_pending_append Enter");
    
    cm_pending *last = cm_pending_head->prev;
    last->next = node;
    node->next = cm_pending_head;
    cm_pending_head->prev = node;
    node->prev = last;
    cm_pending_num++;
    CM_DBG("cm_pending_append Exit");
    
    return 0;
}

int cm_pending_remove_and_destroy(cm_pending * node)
{
    CM_DBG("cm_pending_remove_and_destroy Enter");
        
    free(node->packet);
    node->next->prev = node->prev;
    node->prev->next = node->next;
    free(node);
    cm_pending_num--;
    CM_DBG("cm_pending_remove_and_destroy Exit");
    
    return 0;
}

int cm_pending_list_init()
{
    cm_pending_num = 0;
    cm_pending_head = cm_pending_create();
    cm_pending_head->peer = viadev.me;
    cm_pending_head->prev = cm_pending_head;
    cm_pending_head->next = cm_pending_head;
    return 0;
}

int cm_pending_list_finalize()
{
    while (cm_pending_head->next != cm_pending_head) {
        cm_pending_remove_and_destroy(cm_pending_head->next);
    }
    assert(cm_pending_num==0);
    free(cm_pending_head);
    cm_pending_head = NULL;
    return 0;
}

/*functions for cm protocol*/
int cm_send_ud_msg(cm_msg * msg)
{
    int ret;
    cm_pending *pending;
    struct timeval now;

    CM_DBG("cm_send_ud_msg Enter");
    pending = cm_pending_create();
    if (cm_pending_init(pending, msg)) {
        CM_ERR("cm_pending_init failed");
        return -1;
    }
    cm_pending_append(pending);

    gettimeofday(&now, NULL);
    pending->packet->timestamp = now;
    ret = cm_post_ud_packet(&(pending->packet->payload));
    if (ret) {
        CM_ERR("cm_post_ud_packet failed %d", ret);
        return -1;
    }
    if (cm_pending_num == 1) {
        pthread_cond_signal(&cm_cond_new_pending);
    }
    CM_DBG("cm_send_ud_msg Exit");
    
    return 0;
}

int cm_accept_and_cancel(cm_msg * msg)
{
    cm_msg msg_send;

    /*Prepare QP */
    CM_DBG("cm_accept_and_cancel Enter");
    
    if (cm_enable_qp_init_to_rtr(msg->client_rank,
                                 viadev.qp_hndl[msg->client_rank], msg->lgid,
                                 msg->qpn)) {
        CM_ERR("cm_enable_qp_init_to_rtr failed");
        return -1;
    }

    if (!viadev_use_nfr || 0 == msg->cid) {
        odu_enable_qp(msg->client_rank, viadev.qp_hndl[msg->client_rank]);
    } else {
        nfr_prepare_qp(msg->client_rank);
    }
    
    /*Prepare rep msg */
    memcpy(&msg_send, msg, sizeof(cm_msg));
    msg_send.msg_type = CM_MSG_TYPE_REP;
    msg_send.lgid = cm_lgid[viadev.me];
    if(viadev_use_nfr)
        msg_send.cid = cid[msg->client_rank];
    msg_send.qpn = (viadev.qp_hndl[msg->client_rank])->qp_num;

    /*Send rep msg */
    if (cm_send_ud_msg(&msg_send)) {
        CM_ERR("cm_send_ud_msg failed");
        return -1;
    }

    cm_state_srv[msg->client_rank] = CM_CONN_STATE_S_REQUESTED;

    CM_DBG("cm_accept_and_cancel Cancel");
    /*Cancel client role */
    {
        cm_pending *pending;
        pending =
            cm_pending_search_peer(msg->client_rank, CM_PENDING_CLIENT);
        if (NULL == pending) {
            CM_ERR("Can't find pending entry");
            return -1;
        }

        cm_pending_remove_and_destroy(pending);
        cm_state_cli[msg->client_rank] = CM_CONN_STATE_C_IDLE;
    }
    CM_DBG("cm_accept_and_cancel Exit");
    
    return 0;
}

int cm_accept(cm_msg * msg)
{
    cm_msg msg_send;
    int flag;
    /*Prepare QP */
    CM_DBG("cm_accpet Enter");
    if(!(viadev.qp_hndl[msg->client_rank] = cm_create_rc_qp (msg->client_rank,
                    &flag, 1)))
    {
        CM_ERR("cm_accept: cm_create_rc_qp failed");
        return -1;
    }

#ifdef XRC
    add_qp_xrc_hash (msg->client_rank, viadev.qp_hndl[msg->client_rank]);
#endif

    if (cm_enable_qp_init_to_rtr(msg->client_rank,
                                 viadev.qp_hndl[msg->client_rank], msg->lgid,
                                 msg->qpn)) {
        CM_ERR("cm_enable_qp_init_to_rtr failed");
        return -1;
    }

    if (!viadev_use_nfr || 0 == msg->cid) {
        odu_enable_qp(msg->client_rank, viadev.qp_hndl[msg->client_rank]);
    } else {
        nfr_prepare_qp(msg->client_rank);
    }

    /*Prepare rep msg */
    memcpy(&msg_send, msg, sizeof(cm_msg));
    msg_send.msg_type = CM_MSG_TYPE_REP;
    msg_send.lgid = cm_lgid[viadev.me];
    msg_send.qpn = (viadev.qp_hndl[msg->client_rank])->qp_num;

    /*Send rep msg */
    if (cm_send_ud_msg(&msg_send)) {
        CM_ERR("cm_send_ud_msg failed");
        return -1;
    }

    cm_state_srv[msg->client_rank] = CM_CONN_STATE_S_REQUESTED;
    CM_DBG("cm_accpet Exit");
    return 0;
}
#ifdef XRC

void cm_activate_pending_xrc_conn (int owner_rank)
{
    viadev_conn_queue_t *iter, *tmp;
    viadev_connection_t *c = &(viadev.connections[owner_rank]);
   
    /* We now have a direct QP to owner_rank */ 
    c->xrc_flags |= XRC_FLAG_DIRECT_CONN;
    c->xrc_qp_dst = owner_rank;

    /* Enable any other connection that may be waiting to use this QP */
    iter = c->xrc_conn_queue;

    while (iter)
    {
        tmp = iter->next;
        cm_activate_xrc_qp_reuse (iter->rank);
        free (iter);
        iter = tmp;
    }
    c->xrc_conn_queue = NULL;
}
#endif


int cm_enable(cm_msg * msg)
{
    CM_DBG("cm_enable Enter");
    if (cm_enable_qp_init_to_rts(msg->server_rank,
                viadev.qp_hndl[msg->server_rank],
                msg->lgid, msg->qpn)) {
        CM_ERR("cm_enable_qp_init_to_rtr failed");
        return -1;
    }

    if(viadev_use_nfr) {
        if (0 == msg->cid) {
            odu_enable_qp(msg->server_rank, viadev.qp_hndl[msg->server_rank]);
            cm_conn_state[msg->server_rank] = MPICM_IB_RC_PT2PT;
        } else {
            odu_re_enable_qp(msg->server_rank);
        }
    } else {
        odu_enable_qp(msg->server_rank, viadev.qp_hndl[msg->server_rank]);
        cm_conn_state[msg->server_rank] = MPICM_IB_RC_PT2PT;
    }

    /* Mark QP */
    cm_state_cli[msg->server_rank] = CM_CONN_STATE_C_IDLE;
#ifdef XRC
    if (viadev_use_xrc) {
        cm_activate_pending_xrc_conn (msg->server_rank);
    }
#endif

    /* Mark new connection*/
    viadev.cm_new_connection = 1;

    CM_DBG("cm_enable Exit");
    return 0;
}

static void cm_clean_pending(int peer_rank, int side)
{
    cm_pending *pending;
    pending =
        cm_pending_search_peer(peer_rank, side);
    if (NULL == pending) {
        CM_ERR("Can't find pending entry");
        return;
    }
    cm_pending_remove_and_destroy(pending);
}
/* Reset connection status */
static void cm_nfr_reset_connection(int peer_rank)
{
    cm_conn_state[peer_rank] = MPICM_IB_NONE; /* <<<< pasha ?!*/
    cm_state_cli[peer_rank]  = CM_CONN_STATE_C_IDLE;
    cm_state_srv[peer_rank]  = CM_CONN_STATE_S_IDLE;
}

static int cm_nfr_accept_rec(cm_msg * msg)
{
    int flag = 0;
    cm_msg msg_send;
    /*Prepare QP */
    CM_DBG("cm_nfr_accpet_rec Enter");
    if(!(viadev.qp_hndl[msg->client_rank] = 
            cm_create_rc_qp(msg->client_rank, &flag, 0))) {
        CM_ERR("cm_nfr_accept_rec: cm_create_rc_qp failed");
        return -1;
    }

    if (cm_enable_qp_init_to_rtr(msg->client_rank,
                                 viadev.qp_hndl[msg->client_rank], msg->lgid,
                                 msg->qpn)) {
        CM_ERR("cm_enable_qp_init_to_rtr failed");
        return -1;
    }

    /* odu_enable_qp(msg->client_rank, viadev.qp_hndl[msg->client_rank]); */

    /*Prepare rep msg */
    memcpy(&msg_send, msg, sizeof(cm_msg));
    msg_send.msg_type = CM_MSG_TYPE_REP;
    msg_send.lgid = cm_lgid[viadev.me];
    msg_send.cid = cid[msg->client_rank];
    msg_send.qpn = (viadev.qp_hndl[msg->client_rank])->qp_num;

    /*Send rep msg */
    if (cm_send_ud_msg(&msg_send)) {
        CM_ERR("cm_send_ud_msg failed");
        return -1;
    }

    cm_state_srv[msg->client_rank] = CM_CONN_STATE_S_REQUESTED;
    CM_DBG("cm_nfr_accpet_rec Exit");
    return 0;
}

static int cm_nfr_update_udqp(cm_msg * msg, int peer)
{
    struct ibv_ah_attr ah_attr;

    /* Update LID info */
    memset(&ah_attr, 0, sizeof(ah_attr));
    cm_lgid[peer] = msg->lgid;

    if(viadev_eth_over_ib) {
        ah_attr.is_global = 1;
        ah_attr.grh.hop_limit = 1;
        ah_attr.grh.dgid = cm_lgid[peer].gid;
    } else {
        ah_attr.is_global = 0;
        ah_attr.dlid = cm_lgid[peer].lid;
    }
    ah_attr.sl = viadev_default_service_level;
    ah_attr.src_path_bits = 0;
    ah_attr.port_num = viadev_default_port;
    cm_ah[peer] = ibv_create_ah(viadev.ptag, &ah_attr);
    if (!cm_ah[peer]) {
        CM_ERR("Failed to create AH");
        return -1;
    }

    /* Update QP info */
    viadev.ud_qpn_table[peer] = msg->ud_qpn;
    cm_ud_qpn[peer] = msg->ud_qpn;
}


int cm_handle_msg(cm_msg * msg)
{
    CM_DBG("Handle cm_msg: msg_type: %d, client_rank %d, server_rank %d, cid %d lcid %d",
            msg->msg_type, msg->client_rank, msg->server_rank, msg->cid, cid[msg->client_rank]);
    switch (msg->msg_type) {
        case CM_MSG_TYPE_REQ:
            {
                if (cm_conn_state[msg->client_rank] == MPICM_IB_RC_PT2PT) {
                    /*already existing */

                    if (!viadev_use_nfr || cid[msg->client_rank] >= msg->cid) {
#ifdef XRC
                    if (viadev_use_xrc) {
                        viadev_connection_t *c = &viadev.connections[msg->client_rank];
                        if (viadev.qp_hndl[msg->client_rank] != NULL &&
                                c->xrc_flags & XRC_FLAG_INDIRECT_CONN) {
                            c->xrc_flags |= XRC_FLAG_NEW_QP;
                            cm_accept (msg);
                        }
                    }
#endif
                    return 0;
                    } else {
                        assert(msg->cid - cid[msg->client_rank] == 1);
                        /* NR 
                         * Reconnect request */
                        /* 0. Reconnect after fatal from remote peer ? */
                        if (0 != msg->ud_qpn && cm_ud_qpn[msg->client_rank] != msg->ud_qpn) {
                            if (cm_nfr_update_udqp(msg, msg->client_rank) < 0) {
                                CM_ERR("Failed to update qp num from: %d", msg->client_rank);
                                return -1;
                            }
                        }
                        /* 1. Re-Init server client state */
                        CM_DBG("Got reconnect req in CM_CON state MPICM_IB_RC_PT2PT from %d RCID[%d] LCID[%d]",
                                msg->client_rank, msg->cid,  cid[msg->client_rank]);
                        cm_nfr_reset_connection(msg->client_rank);
                        /* 2. Update local cid */
                        cid[msg->client_rank] = msg->cid;
                        /* 3. Create new QP */
                        /* 4. Move it from INIT to RTR */
                        /* 5. Send Reply */
                        cm_nfr_accept_rec(msg); /* Steps 3-4-5 */
                        /* We do not touch the OLD qp, MPI will detect the error on MPI level
                         * and will try to reconnect. It will see that QP number is different from
                         * new one and will not try to reconect */

                    }
                } else if (cm_state_srv[msg->client_rank] !=
                        CM_CONN_STATE_S_IDLE) {
                    if (!viadev_use_nfr || cid[msg->client_rank] >= msg->cid) {
                        /*already a pending request from that peer */
                        return 0;
                    } else {
                        cm_pending *pending;

                        assert(msg->cid - cid[msg->client_rank] == 1);
                        CM_DBG("Got reconnect req in CM_CON state !CM_CONN_STATE_S_IDLE from %d RCID[%d] LCID[%d]",
                                msg->client_rank, msg->cid,  cid[viadev.me]);
                        /* NR 
                         * Reconnect request, we need close existing QP, create new one
                         * and update local cid value */
                        /* We may close the exist qp because it never was used by upper layer */
                        /* 0. Reconnect after fatal from remote peer ? */
                        if (0 != msg->ud_qpn && cm_ud_qpn[msg->client_rank] != msg->ud_qpn) {
                            if (cm_nfr_update_udqp(msg, msg->client_rank) < 0) {
                                CM_ERR("Failed to update qp num from: %d", msg->client_rank);
                                return -1;
                            }
                        }
                        /* 1. Re-Init server client state */
                        cm_nfr_reset_connection(msg->client_rank);
                        /* 2. Cancel Server rep messages */
                        cm_clean_pending(msg->client_rank, CM_PENDING_SERVER);
                        /* 3. increase cid */
                        cid[msg->client_rank] = msg->cid;
                        /* 5. Create new QP */
                        /* 6. Move it from INIT to RTR */
                        /* 7. reply with new data */
                        cm_nfr_accept_rec(msg); /* Steps 4-5-6 */

                    }
                } else if (cm_state_cli[msg->client_rank] !=
                        CM_CONN_STATE_C_IDLE) {

                    if (viadev_use_nfr)
                        assert(cid[msg->client_rank] == msg->cid);

                    /*already initiated a request to that peer */
                    CM_DBG("Got reconnect req in CM_CON state !CM_CONN_STATE_C_IDLE from %d RCID[%d] LCID[%d]",
                            msg->client_rank, msg->cid,  cid[msg->client_rank]);

                    /* Reconnect after fatal from remote peer ? */

                    if (NO_FATAL != nfr_fatal_error) {
                        /*that peer should be server */
                        return 0;
                    }

                    if (viadev_use_nfr && 0 != msg->ud_qpn && cm_ud_qpn[msg->client_rank] != msg->ud_qpn) {
                        if (cm_nfr_update_udqp(msg, msg->client_rank) < 0) {
                            CM_ERR("Failed to update qp num from: %d", msg->client_rank);
                            return -1;
                        }
                        /* The fatal site must be client and this site must be server ! */
                        cid[msg->client_rank] = msg->cid;
                        /*myself should be server */
                        cm_accept_and_cancel(msg);
                    } else {
                        if (msg->client_rank > viadev.me) {
                            /*that peer should be server */
                            return 0;
                        } else {
                            if(viadev_use_nfr) 
                                cid[msg->client_rank] = msg->cid;

                            /*myself should be server */
                            cm_accept_and_cancel(msg);
                        }
                    }
                } else {
                    /* First connection request, we do not care for CID */
                    if (viadev_use_nfr && msg->cid > 0) {
                        assert(msg->cid - cid[msg->client_rank] == 1);
                        cid[msg->client_rank] = msg->cid;
                    }
                    CM_DBG("Got reconnect req in accept from %d RCID[%d] LCID[%d]",
                            msg->client_rank, msg->cid,  cid[viadev.me]);
                    cm_accept(msg);
                }

#if 0
                    /*already initiated a request to that peer */
                    if (msg->client_rank > viadev.me) {
                        /*that peer should be server */
                        return 0;
                    } else {
                        /*myself should be server */
                        cm_accept_and_cancel(msg);
                    }
                } else {
                    cm_accept(msg);
                }
#endif
            }
            break;
        case CM_MSG_TYPE_REP:
            {

                if(viadev_use_nfr) {
                    if (cid[msg->server_rank] == msg->cid) {
                        CM_DBG("cm_conn_state[%d]=%d, cm_state_srv[%d]=%d, cm_state_cli[%d]=%d",
                                msg->server_rank, cm_conn_state[msg->server_rank],
                                msg->server_rank, cm_state_srv[msg->server_rank],
                                msg->server_rank, cm_state_cli[msg->server_rank]);
                        cm_pending *pending;
                        if (cm_state_cli[msg->server_rank] !=
                                CM_CONN_STATE_C_REQUESTING) {
                            /*not waiting for any reply */
                            CM_DBG("Not waiting for reply");
                            return 0;
                        }
                        pending =
                            cm_pending_search_peer(msg->server_rank,
                                    CM_PENDING_CLIENT);
                        if (NULL == pending) {
                            CM_ERR("Can't find pending entry");
                            return -1;
                        }
                        cm_pending_remove_and_destroy(pending);
                        cm_enable(msg);
                    }
                } else {
                    cm_pending *pending;
                    if (cm_state_cli[msg->server_rank] !=
                            CM_CONN_STATE_C_REQUESTING) {
                        /*not waiting for any reply */
                        return 0;
                    }
                    pending =
                        cm_pending_search_peer(msg->server_rank,
                                CM_PENDING_CLIENT);
                    if (NULL == pending) {
                        CM_ERR("Can't find pending entry");
                        return -1;
                    }
                    cm_pending_remove_and_destroy(pending);
                    cm_enable(msg);
                }
            }
            break;
        default:
            CM_ERR("Unknown msg type: %d", msg->msg_type);
            return -1;
    }
    CM_DBG("cm_handle_msg Exit");
    return 0;
}

void *cm_timeout_handler(void *arg)
{
    struct timeval now;
    int delay, ret;
    cm_pending *p;
    struct timespec remain;

    /* This thread should be in a cancel enabled state */
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    while (1) {
        MPICM_Lock();
        while (cm_pending_num == 0) {
            pthread_cond_wait(&cm_cond_new_pending, &cm_conn_state_lock);
        }
        
        while (1) {
            MPICM_Unlock();
            nanosleep(&cm_timeout,&remain);/*Not handle the EINTR*/
            MPICM_Lock();
            if (cm_pending_num == 0) {
                break;
            }
            CM_DBG("Time out");
            p = cm_pending_head;
            if (NULL == p) {
                CM_ERR("cm_pending_head corrupted");
            }
            gettimeofday(&now, NULL);
            while (p->next != cm_pending_head) {
                p = p->next;
                delay = (now.tv_sec - p->packet->timestamp.tv_sec) * 1000000
                    + (now.tv_usec - p->packet->timestamp.tv_usec);
                if (delay > cm_timeout_usec) {       /*Timer expired */
                    CM_DBG("Resend");
                    p->packet->timestamp = now;
                    ret = cm_post_ud_packet(&(p->packet->payload));
                    if (ret) {
                        CM_ERR("cm_post_ud_packet failed %d", ret);
                    }
                    gettimeofday(&now,NULL);
                }
            }
            CM_DBG("Time out exit");
        }
        MPICM_Unlock();
    }
    return NULL;
}

void *cm_completion_handler(void *arg)
{
    /* This thread should be in a cancel enabled state */
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    
    while (1) {
        struct ibv_cq *ev_cq;
        void *ev_ctx;
        struct ibv_wc wc;
        int ne;
        int spin_count;
        int ret;

        CM_DBG("Waiting for cm message");

        do {
            ret = ibv_get_cq_event(cm_ud_comp_ch, &ev_cq, &ev_ctx);
            if (ret && errno != EINTR ) {
                CM_ERR("Failed to get cq_event: %d", ret);
                return NULL;
            }
        } while(ret && errno == EINTR);
        
        ibv_ack_cq_events(ev_cq, 1);

        if (ev_cq != cm_ud_recv_cq) {
            CM_ERR("CQ event for unknown CQ %p", ev_cq);
            return NULL;
        }

        CM_DBG("Processing cm message");
          
        spin_count = 0;
        do {
            ne = ibv_poll_cq(cm_ud_recv_cq, 1, &wc);
            if (ne < 0) {
                CM_ERR("poll CQ failed %d", ne);
                return NULL;
            } else if (ne == 0) {
                spin_count++;
                continue;
            }

            spin_count = 0;

            if (wc.status != IBV_WC_SUCCESS) {
                CM_ERR("Failed status %d for wr_id %d",
                        wc.status, (int) wc.wr_id);
                return NULL;
            }

            if (wc.wr_id == CM_UD_RECV_WR_ID) {
                void *buf =
                    cm_ud_recv_buf +
                    cm_ud_recv_buf_index * (sizeof(cm_msg) + 40) + 40;
                cm_msg *msg = (cm_msg *) buf;
                if (msg->msg_type == CM_MSG_TYPE_FIN_SELF) {
                    CM_DBG("received finalization message");
                    return NULL;
                }
                MPICM_Lock();
                cm_handle_msg(msg);
                CM_DBG("Post recv");
                cm_post_ud_recv(buf - 40, sizeof(cm_msg));
                cm_ud_recv_buf_index =
                    (cm_ud_recv_buf_index + 1) % cm_recv_buffer_size;
                MPICM_Unlock();
            }
        }while (spin_count < cm_max_spin_count);

        CM_DBG("notify_cq");
        if ((ret = ibv_req_notify_cq(cm_ud_recv_cq, 1))) {
            CM_ERR("Couldn't request CQ notification: ret=%d",ret);
            return NULL;
        }
    }
    return NULL;
}

int MPICM_Init_UD(uint32_t * ud_qpn)
{
    int i, ret;
    char *value;

    /*Initialization */
    cm_conn_state = malloc(viadev.np * sizeof(MPICM_conn_state));
    cm_state_cli = malloc(viadev.np * sizeof(CM_conn_state_cli));
    cm_state_srv = malloc(viadev.np * sizeof(CM_conn_state_srv));
    cm_ah = malloc(viadev.np * sizeof(struct ibv_ah *));
    cm_ud_qpn = malloc(viadev.np * sizeof(uint32_t));
    cm_lgid = malloc(viadev.np * sizeof(lgid));

    if (viadev_use_nfr && NO_FATAL == nfr_fatal_error) {
        cid = calloc(viadev.np, sizeof(uint16_t));
    }

    cm_req_id_global = 0;

    page_size = sysconf(_SC_PAGESIZE);

    if ((value = getenv("VIADEV_CM_SEND_DEPTH")) != NULL) {
        cm_send_depth = atoi(value);
    } else {
        cm_send_depth = DEFAULT_CM_SEND_DEPTH;
    }

    if ((value = getenv("VIADEV_CM_RECV_BUFFERS")) != NULL) {
        cm_recv_buffer_size = atoi(value);
    } else {
        cm_recv_buffer_size = DEFAULT_CM_MSG_RECV_BUFFER_SIZE;
    }

    if ((value = getenv("VIADEV_CM_UD_PSN")) != NULL) {
        cm_ud_psn = atoi(value);
    } else {
        cm_ud_psn = CM_UD_DEFAULT_PSN;
    }

    if ((value = getenv("VIADEV_CM_MAX_SPIN_COUNT")) != NULL) {
        cm_max_spin_count = atoi(value);
    } else {
        cm_max_spin_count = DEFAULT_CM_MAX_SPIN_COUNT;
    }
    
    if ((value = getenv("VIADEV_CM_THREAD_STACKSIZE")) != NULL) {
        cm_thread_stacksize = atoi(value);
    } else {
        cm_thread_stacksize = DEFAULT_CM_THREAD_STACKSIZE;
    }
   
    if ((value = getenv("VIADEV_CM_TIMEOUT")) != NULL) {
        cm_timeout_usec = atoi(value)*1000;
    } else { 
        cm_timeout_usec = CM_DEFAULT_TIMEOUT;
    }
    if (cm_timeout_usec < CM_MIN_TIMEOUT) {
        cm_timeout_usec = CM_MIN_TIMEOUT;
    }

    cm_timeout.tv_sec = cm_timeout_usec/1000000;
    cm_timeout.tv_nsec = (cm_timeout_usec-cm_timeout.tv_sec*1000000)*1000;

    cm_ud_buf =
        memalign(page_size,
                 (sizeof(cm_msg) + 40) * (cm_recv_buffer_size + 1));
    if (!cm_ud_buf) {
        CM_ERR("Couldn't allocate work buf");
        return -1;
    }
    
    memset(cm_ud_buf, 0,
           (sizeof(cm_msg) + 40) * (cm_recv_buffer_size + 1));
    cm_ud_send_buf = cm_ud_buf;
    cm_ud_recv_buf = cm_ud_buf + sizeof(cm_msg) + 40;

    cm_ud_comp_ch = ibv_create_comp_channel(viadev.context);
    if (!cm_ud_comp_ch) {
        CM_ERR("Couldn't create completion channel");
        return -1;
    }

    cm_ud_mr = ibv_reg_mr(viadev.ptag, cm_ud_buf,
                          (sizeof(cm_msg) +
                           40) * (cm_recv_buffer_size + 1),
                          IBV_ACCESS_LOCAL_WRITE);
    if (!cm_ud_mr) {
        CM_ERR("Couldn't allocate MR");
        return -1;
    }

    cm_ud_recv_cq =
        ibv_create_cq(viadev.context, cm_recv_buffer_size, NULL,
                      cm_ud_comp_ch, 0);
    if (!cm_ud_recv_cq) {
        CM_ERR("Couldn't create CQ");
        return -1;
    }

    cm_ud_send_cq =
        ibv_create_cq(viadev.context, cm_send_depth, NULL, NULL, 0);
    if (!cm_ud_send_cq) {
        CM_ERR("Couldn't create CQ");
        return -1;
    }

    {
        struct ibv_qp_init_attr attr;
        memset(&attr, 0, sizeof(struct ibv_qp_init_attr));
        attr.send_cq = cm_ud_send_cq;
        attr.recv_cq = cm_ud_recv_cq;
        attr.cap.max_send_wr = cm_send_depth;
        attr.cap.max_recv_wr = cm_recv_buffer_size;
        attr.cap.max_send_sge = 1;
        attr.cap.max_recv_sge = 1;
        attr.qp_type = IBV_QPT_UD;

        cm_ud_qp = ibv_create_qp(viadev.ptag, &attr);
        if (!cm_ud_qp) {
            CM_ERR("Couldn't create UD QP");
            return -1;
        }
    }

    *ud_qpn = cm_ud_qp->qp_num;
    {
        struct ibv_qp_attr attr;
        memset(&attr, 0, sizeof(struct ibv_qp_attr));

        attr.qp_state = IBV_QPS_INIT;
	    attr.port_num = viadev_default_port;
        set_pkey_index(&attr.pkey_index,viadev_default_port);
        attr.qkey = 0;

        if ((ret = ibv_modify_qp(cm_ud_qp, &attr,
                                 IBV_QP_STATE |
                                 IBV_QP_PKEY_INDEX |
                                 IBV_QP_PORT | IBV_QP_QKEY))) {
            CM_ERR("Failed to modify QP to INIT, ret = %d, errno=%d",
                    ret, errno);
            return -1;
        }
    }
    {
        struct ibv_qp_attr attr;

        memset(&attr, 0, sizeof(struct ibv_qp_attr));
        attr.qp_state = IBV_QPS_RTR;
        if ((ret = ibv_modify_qp(cm_ud_qp, &attr, IBV_QP_STATE))) {
            CM_ERR("Failed to modify QP to RTR: ret=%d",ret);
            return -1;
        }
    }

    {
        struct ibv_qp_attr attr;
        memset(&attr, 0, sizeof(struct ibv_qp_attr));

        attr.qp_state = IBV_QPS_RTS;
        attr.sq_psn = cm_ud_psn;
        ret = ibv_modify_qp(cm_ud_qp, &attr, IBV_QP_STATE | IBV_QP_SQ_PSN); 
        if(ret){
            CM_ERR("Failed to modify QP to RTS: ret=%d",ret);
            return -1;
        }
    }

    for (i = 0; i < cm_recv_buffer_size; i++) {
        /*  cm_msg * msg = (cm_msg *)(cm_ud_recv_buf+(sizeof(cm_msg)+40)*i+40); */
        if (cm_post_ud_recv
            (cm_ud_recv_buf + (sizeof(cm_msg) + 40) * i, sizeof(cm_msg))) {
            CM_ERR("cm_post_ud_recv failed");
            return -1;
        }
    }
    cm_ud_recv_buf_index = 0;

    if (ret = ibv_req_notify_cq(cm_ud_recv_cq, 1)) {
        CM_ERR("Couldn't request CQ notification : ret=%d",ret);
        return -1;
    }

    for (i = 0; i < viadev.np; i++) {
        cm_conn_state[i] = MPICM_IB_NONE;
        cm_state_cli[i] = CM_CONN_STATE_C_IDLE;
        cm_state_srv[i] = CM_CONN_STATE_S_IDLE;
    }

    viadev.cm_new_connection = 0;

    cm_pending_list_init();
    return 0;
}

int MPICM_Connect_UD(uint32_t * qpns, lgid *lgids)
{
    int i, ret;
    pthread_attr_t attr;
    if (pthread_attr_init(&attr))
    {
        CM_ERR("pthread_attr_init failed\n");
        return -1;
    }
    
    /*Copy qpns and lgids */
    memcpy(cm_ud_qpn, qpns, viadev.np * sizeof(uint32_t));
    memcpy(cm_lgid, lgids, viadev.np * sizeof(lgid));

    /*Create address handles */
    for (i = 0; i < viadev.np; i++) {
        struct ibv_ah_attr ah_attr;
        memset(&ah_attr, 0, sizeof(ah_attr));

        if(viadev_eth_over_ib) {
            ah_attr.is_global = 1;
            ah_attr.grh.hop_limit = 1;
            ah_attr.grh.dgid = lgids[i].gid;
        } else {
            ah_attr.is_global = 0;
            ah_attr.dlid = lgids[i].lid;
        }
        ah_attr.sl = viadev_default_service_level;
        ah_attr.src_path_bits = 0;
        ah_attr.port_num = viadev_default_port;
        cm_ah[i] = ibv_create_ah(viadev.ptag, &ah_attr);
        if (!cm_ah[i]) {
            CM_ERR("Failed to create AH, errno=%d", errno);
            return -1;
        }
    }

    /*Spawn cm thread */
    ret = pthread_attr_setstacksize(&attr,cm_thread_stacksize);
    if (ret && ret != EINVAL) 
    {
        CM_ERR("pthread_attr_setstacksize failed\n");
        return -1;
    }
    pthread_create(&cm_comp_thread, &attr, cm_completion_handler, NULL);
    pthread_create(&cm_timer_thread, &attr, cm_timeout_handler, NULL);
    return 0;
}

int MPICM_Finalize_UD()
{
    int i;
    int rc;
    CM_DBG("In MPICM_Finalize_UD");

    cm_pending_list_finalize();
    {
        /*Cancel cm thread */
        cm_msg msg;
        struct ibv_sge list;
        struct ibv_send_wr wr;
        struct ibv_send_wr *bad_wr;
        struct ibv_wc wc;
        msg.msg_type = CM_MSG_TYPE_FIN_SELF;
        memcpy(cm_ud_send_buf + 40, &msg, sizeof(cm_msg));
        memset(&list, 0, sizeof(struct ibv_sge));
        list.addr = (uintptr_t) cm_ud_send_buf + 40;
        list.length = sizeof(cm_msg);
        list.lkey = cm_ud_mr->lkey;

        memset(&wr, 0, sizeof(struct ibv_send_wr));
        wr.wr_id = CM_UD_SEND_WR_ID;
        wr.sg_list = &list;
        wr.num_sge = 1;
        wr.opcode = IBV_WR_SEND;
        wr.send_flags = IBV_SEND_SIGNALED | IBV_SEND_SOLICITED;
        wr.wr.ud.ah = cm_ah[viadev.me];
        wr.wr.ud.remote_qpn = cm_ud_qpn[viadev.me];
        wr.wr.ud.remote_qkey = 0;

        if ((rc = ibv_post_send(cm_ud_qp, &wr, &bad_wr))) {
            CM_ERR("ibv_post_send to ud qp failed: rc=%d",rc);
        }
    }

    if (pthread_join(cm_comp_thread, NULL)) {
        CM_ERR("Failed to join cm_comp_thread thread\n");
    }
    if (pthread_cancel(cm_timer_thread)) {
        CM_ERR("Failed to cancel cm_timer_thread thread\n");
    }
    if (pthread_join(cm_timer_thread, NULL)) {
        CM_ERR("Failed to join cm_timer_thread thread\n");
    }

    /*Clean up */
    for (i = 0; i < viadev.np; i++) {
        if ((rc = ibv_destroy_ah(cm_ah[i]))) {
            CM_ERR("ibv_destroy_ah failed: rc=%d", rc);
            return -1;
        }
    }
            
    if ((rc=ibv_destroy_qp(cm_ud_qp))) {
        CM_ERR("ibv_destroy_qp failed: rc=%d", rc);
        return -1;
    }

    if (( rc = ibv_destroy_cq(cm_ud_recv_cq))) {
        CM_ERR("ibv_destroy_cq cm_ud_recv_cq failed: rc=%d", rc);
        return -1;
    }
    
    if (( rc = ibv_destroy_cq(cm_ud_send_cq))) {
        CM_ERR("ibv_destroy_cq cm_ud_send_cq failed: rc=%d", rc);
        return -1;
    }
    
    if (( rc = ibv_destroy_comp_channel(cm_ud_comp_ch))) {
        CM_ERR("ibv_destroy_comp_channel failed: rc=%d", rc);
        return -1;
    }

    if (( rc = ibv_dereg_mr(cm_ud_mr))) {
        CM_ERR("ibv_dereg_mr failed: rc=%d", rc);
        return -1;
    }

    if (cm_ud_buf)
        free(cm_ud_buf);
    if (cm_conn_state)
        free((void *)cm_conn_state);
    if (cm_state_cli)
        free(cm_state_cli);
    if (cm_state_srv)
        free(cm_state_srv);
    if (cm_ah)
        free(cm_ah);
    if (cm_ud_qpn)
        free(cm_ud_qpn);
    if (cm_lgid)
        free(cm_lgid);
    if (viadev_use_nfr && NO_FATAL == nfr_fatal_error) {
        if (cid)
            free(cid);
    }


    CM_DBG("MPICM_Finalize_UD done");
    return 0;
}

int MPICM_Reconnect_req(int peer_rank)
{
    int flag = 0;
    cm_msg req;
    viadev_connection_t *c = &viadev.connections[peer_rank];

    CM_DBG("NR re-conn request to %d state %d cli %d srv %d\n",
           peer_rank, cm_conn_state[peer_rank], cm_state_cli[peer_rank], cm_state_srv[peer_rank]);
    if (viadev.me == peer_rank) {
        return -1;
    }
    MPICM_Lock();
    if (cm_conn_state[peer_rank] == MPICM_IB_RC_PT2PT) {
        /* If QP number is different - we are already reconnected */
        if (viadev.connections[peer_rank].vi->qp_num !=
                viadev.qp_hndl[peer_rank]->qp_num) {
            CM_DBG("NR re-conn already reconnected %d\n",peer_rank);
            return 0;
        } else {
            cm_conn_state[peer_rank] = MPICM_IB_NONE;
        }
    }

    if (cm_state_cli[peer_rank] == CM_CONN_STATE_C_REQUESTING) {
        CM_DBG("NR %d in CM_CONN_STATE_C_REQUESTING \n",peer_rank);
        MPICM_Unlock();
        return 0;
    }
    if (cm_state_srv[peer_rank] != CM_CONN_STATE_S_IDLE) {
        /*handling the request from peer */
        CM_DBG("NR %d in CM_CONN_STATE_S_IDLE\n",peer_rank);
        MPICM_Unlock();
        return 0;
    }

    cm_state_cli[peer_rank] = CM_CONN_STATE_C_REQUESTING;
    ++cid[peer_rank];
    req.cid = cid[peer_rank];
    if(!(viadev.qp_hndl[peer_rank] = 
                cm_create_rc_qp(peer_rank, &flag, 0))) {
        CM_ERR("MPICM_Reconnect_req: cm_create_rc_qp failed\n");
        return -1;
    }
    req.msg_type = CM_MSG_TYPE_REQ;
    req.client_rank = viadev.me;
    req.server_rank = peer_rank;
    req.req_id = ++cm_req_id_global;
    req.lgid = cm_lgid[viadev.me];

    if (1 == c->in_restart) {
        req.ud_qpn = cm_ud_qp->qp_num; /* put new qp numbers */
    } else {
        req.ud_qpn = 0;
    }

    req.qpn = viadev.qp_hndl[req.server_rank]->qp_num;
    CM_DBG("Sending Req to rank %d message %d c %d s %d id %d lid %d qp %d cid %d %d\n", peer_rank, req.msg_type, req.client_rank, req.server_rank, req.req_id, req.lid, req.qpn, req.cid, cid[peer_rank]);
    if (cm_send_ud_msg(&req)) {
        CM_ERR("cm_send_ud_msg failed");
        return -1;
    }
    MPICM_Unlock();
    return 0;
}

int MPICM_Connect_req(int peer_rank)
{
    cm_msg req;
    int flag;
    CM_DBG("Conn request to %d state %d cli %d srv %d\n",
           peer_rank, cm_conn_state[peer_rank], cm_state_cli[peer_rank], cm_state_srv[peer_rank]);

    if (viadev.me == peer_rank) {
        return -1;
    }

    MPICM_Lock();
    if (cm_conn_state[peer_rank] == MPICM_IB_RC_PT2PT
#ifdef XRC
            || cm_conn_state[peer_rank] == MPICM_IB_XRC_PENDING
#endif
            ) {
        MPICM_Unlock();
        return 0;
    }
    if (cm_state_cli[peer_rank] == CM_CONN_STATE_C_REQUESTING) {
        MPICM_Unlock();
        return 0;
    }
    if (cm_state_srv[peer_rank] != CM_CONN_STATE_S_IDLE) {
        /*handling the request from peer */
        MPICM_Unlock();
        return 0;
    }

    if (NULL != viadev.qp_hndl[peer_rank]) {
        /* We in reconnect mode */
        MPICM_Unlock();
        return 0;
    } 

    CM_DBG("Sending Req to rank %d", peer_rank);
    cm_state_cli[peer_rank] = CM_CONN_STATE_C_REQUESTING;
    flag = 0;
    if (viadev_use_nfr) {
        req.cid = cid[peer_rank];
    }
    if(!(viadev.qp_hndl[peer_rank] = cm_create_rc_qp(peer_rank, &flag, 0)))
    {
        CM_ERR("MPICM_Connect_req: cm_create_rc_qp failed");
        return -1;
    }

    if (flag == XRC_QP_NEW) {
#ifdef XRC
        add_qp_xrc_hash (peer_rank, viadev.qp_hndl[peer_rank]);
#endif 
        req.client_rank = viadev.me;
        req.server_rank = peer_rank;
        req.req_id = ++cm_req_id_global;
        req.lgid = cm_lgid[viadev.me];
        req.msg_type = CM_MSG_TYPE_REQ;
        req.ud_qpn = 0;
        req.qpn = viadev.qp_hndl[req.server_rank]->qp_num;

        if (cm_send_ud_msg(&req)) {
            CM_ERR("cm_send_ud_msg failed");
            return -1;
        }
    }
#ifdef XRC
    else {
        if (cm_conn_state[viadev.connections[peer_rank].xrc_qp_dst] 
                == MPICM_IB_RC_PT2PT) {
            cm_activate_xrc_qp_reuse (peer_rank);
        }
        else {
            viadev_connection_t *c_owner = &(viadev.connections
                [viadev.connections[peer_rank].xrc_qp_dst]);
            viadev_conn_queue_t *cq = (viadev_conn_queue_t *) malloc 
                (viadev_conn_queue_s);
            cq->rank = peer_rank;
            cq->next = c_owner->xrc_conn_queue;
            c_owner->xrc_conn_queue = cq;
            cm_conn_state[peer_rank] = MPICM_IB_XRC_PENDING;
        }
    }
#endif
    MPICM_Unlock();
    return 0;
}

int MPICM_Server_connection_establish(int peer_rank)
{
    cm_pending *pending;
    viadev_connection_t *c;
    MPICM_Lock();
    if (cm_state_srv[peer_rank] != CM_CONN_STATE_S_REQUESTED) {
        /*not waiting for comfirm */
        MPICM_Unlock();
        return 0;
    }
    pending = cm_pending_search_peer(peer_rank, CM_PENDING_SERVER);
    if (NULL == pending) {
        CM_ERR("Can't find pending entry");
        return -1;
    }
    cm_pending_remove_and_destroy(pending);
    if (cm_enable_qp_rtr_to_rts(viadev.qp_hndl[peer_rank])) {
        CM_ERR("cm_enable_qp_rtr_to_rts failed");
        return -1;
    }
    cm_state_srv[peer_rank] = CM_CONN_STATE_S_IDLE;
    cm_conn_state[peer_rank] = MPICM_IB_RC_PT2PT;
#ifdef XRC
    if (viadev_use_xrc) {
        c = &(viadev.connections[peer_rank]);
        if (c->xrc_flags & XRC_FLAG_NEW_QP) {
            c->xrc_flags &= ~XRC_FLAG_INDIRECT_CONN;
            c->xrc_flags ^= XRC_FLAG_NEW_QP;

            /* Replace indirect connection with direct one */
            c->vi = viadev.qp_hndl[peer_rank];
        }
        cm_activate_pending_xrc_conn (peer_rank);
    }
#endif

    MPICM_Unlock();
    CM_DBG("MPICM_Server_connection_establish, peer_rank %d", peer_rank);
    return 0;
}

