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
#include <fcntl.h>

#ifdef XRC

/* determine the unique hosts in the system */
typedef struct {
    int hostid; 
    int rank;
} xrc_sort_container_t;

static int compare_xrc_container(const void *a, const void *b)
{
    int diff = ((xrc_sort_container_t *) a)->hostid -
        ((xrc_sort_container_t *) b)->hostid;

    if(diff > 0) {
        return 1;
    } else if (diff < 0) {
        return -1;
    } else {
        return 0;
    }
}

void MV_XRC_Init(int *allhostids, int nhostids, int global_id, char *my_name)
{
    int i = 0, j = 0;
    int nuniq = 0;
    char *xrc_file = NULL;
    mvdev_xrc_info_t *xrc_info;
    xrc_sort_container_t * containers =
        malloc(sizeof(xrc_sort_container_t) * nhostids);

    xrc_info = mvdev.xrc_info = 
        (mvdev_xrc_info_t *) malloc(sizeof(mvdev_xrc_info_t));

    /* get the unique hosts */
    for(i = 0; i < nhostids; i++) {
        containers[i].hostid = allhostids[i];
        if(!mvparams.xrcshared) {
            containers[i].hostid = i;
        }
        containers[i].rank = i;
    }

    qsort((void *) containers, nhostids,
            sizeof(xrc_sort_container_t), compare_xrc_container);
    {
        int last = -1;
        i = 0;
        while(i < nhostids) {
            if(containers[i].hostid != last) {
                nuniq++;
                last = containers[i].hostid;
            }
            i++;
        }
    }

    xrc_info->uniq_hosts = nuniq;
    xrc_info->connections = (mvdev_channel_xrc_shared *)
        malloc(sizeof(mvdev_channel_xrc_shared) * nuniq);

    /* create pointers from each connection to the shared
     * XRC connection
     */

    {
        i = j = 0;
        int last = -1;
        mvdev_channel_xrc_shared * current_conn = NULL;
        while(i < nhostids) {
            if(containers[i].hostid != last) {
                current_conn = &(xrc_info->connections[j]);
                current_conn->xtra_xrc_next = NULL;
                current_conn->is_xtra = 0;
                current_conn->status = MV_XRC_CONN_SHARED_INIT;
                current_conn->xrc_channel_ptr = &(mvdev.connections[containers[i].rank]);
                pthread_spin_init(&(current_conn->lock), 0);
                last = containers[i].hostid;
                j++;
                MV_ASSERT(j <= nuniq);
            } else {
                mvdev_connection_t * c = current_conn->xrc_channel_ptr;
                while(NULL != c->xrc_channel_ptr) {
                   c = c->xrc_channel_ptr; 
                }
                c->xrc_channel_ptr = &(mvdev.connections[containers[i].rank]);
            }



            /* init the per rank data structure */
            mvdev.connections[containers[i].rank].xrc_channel.shared_channel = current_conn;
            mvdev.connections[containers[i].rank].xrc_channel.remote_srqn_head = NULL;
            mvdev.connections[containers[i].rank].xrc_channel.remote_srqn_tail = NULL;
            mvdev.connections[containers[i].rank].xrc_channel.status = MV_XRC_CONN_SINGLE_INIT;

            i++;
        }
    }

    for(i = 0; i < mvdev.num_hcas; i++) {
        if(!(mvdev.hca[i].device_attr.device_cap_flags & IBV_DEVICE_XRC)) {
            error_abort_all (IBV_RETURN_ERR, "Selected HCA doesn't support XRC\n");
        }

        if(mvparams.xrcshared) {
            /* Open the XRC domain */
            xrc_file = (char *) malloc (sizeof (char) *
                    (255 /* Max Hostname len */
                     + 26 /* space for exec name */
                     + 22 /* space for UID */
                     + 3  /* space for HCAs */));

            sprintf(xrc_file, "/dev/shm/mvapich-xrc-%d-%s-%d-%d.tmp", global_id,
                    my_name, getuid(), i);

            xrc_info->fd[i] = open(xrc_file, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);

            if(xrc_info->fd[i] < 0) {
                perror("cannot open XRC file");
                fprintf(stderr, "Error creating XRC file!, %s\n", xrc_file);
            }

            xrc_info->xrc_domain[i] = 
                ibv_open_xrc_domain(mvdev.hca[i].context, 
                        xrc_info->fd[i], O_CREAT);
        } else {
            xrc_info->xrc_domain[i] = 
                ibv_open_xrc_domain(mvdev.hca[i].context, 
                        -1, O_CREAT);

        }

        if(NULL == xrc_info->xrc_domain[i]) {
            fprintf(stderr, "Error creating XRC domain!\n");
        }

        free(xrc_file);
    }

    MV_Create_XRC_SRQs();

    free(containers);
}


struct ibv_srq * MV_Create_XRC_SRQ_aux(
        struct ibv_xrc_domain *xrc_domain,
        struct ibv_context *ctxt, struct ibv_pd *pd,
        struct ibv_cq *cq, int srq_size, int srq_limit)
{
    struct ibv_srq_init_attr srq_init_attr;
    struct ibv_srq *srq_ptr = NULL;

    memset(&srq_init_attr, 0, sizeof(srq_init_attr));

    srq_init_attr.srq_context = ctxt;
    srq_init_attr.attr.max_wr = srq_size;
    srq_init_attr.attr.max_sge = 1;
    /* The limit value should be ignored during SRQ create */
    srq_init_attr.attr.srq_limit = srq_limit;

    srq_ptr = ibv_create_xrc_srq(pd, xrc_domain,
            cq, &srq_init_attr);

    if (!srq_ptr) {
        fprintf(stderr, "Error creating XRC SRQ\n");
    }

    return srq_ptr;
}

mv_srq_shared * MV_Create_XRC_SRQ_Block(int buf_size) {
    int i;

    mv_srq_shared * srq_block = (mv_srq_shared *)
        malloc(sizeof(mv_srq_shared));

    for(i = 0; i < mvdev.num_hcas; i++) {
        mv_srq * srq = (mv_srq *) malloc(sizeof(mv_srq));
        mv_rpool * new_rpool;

        srq->srq = MV_Create_XRC_SRQ_aux( mvdev.xrc_info->xrc_domain[i], 
                mvdev.hca[i].context, mvdev.hca[i].pd, 
                mvdev.cq[i], mvparams.srq_size, 20);
        srq->limit = 20;
        srq->buffer_size = buf_size;

        srq_block->pool[i] = new_rpool = 
            MV_Create_RPool(mvparams.srq_size, 100, buf_size, srq, NULL);
        CHECK_RPOOL(new_rpool);
    }

    return srq_block;
}

void MV_Create_XRC_SRQs() {
    mv_srq_shared * curr_block;
    if(mvparams.msrq) {
        mvdev.xrc_srqs   = MV_Create_XRC_SRQ_Block(128);
        curr_block = mvdev.xrc_srqs;
        curr_block->next = MV_Create_XRC_SRQ_Block(256);
        curr_block = curr_block->next;
        curr_block->next = MV_Create_XRC_SRQ_Block(512);
        curr_block = curr_block->next;
        curr_block->next = MV_Create_XRC_SRQ_Block(1024);
        curr_block = curr_block->next;
        curr_block->next = MV_Create_XRC_SRQ_Block(2048);
        curr_block = curr_block->next;
        curr_block->next = MV_Create_XRC_SRQ_Block(4096);
        curr_block = curr_block->next;
        curr_block->next = MV_Create_XRC_SRQ_Block(8192);
        curr_block = curr_block->next;
        curr_block->next = NULL;
    } else {
        mvdev.xrc_srqs   = MV_Create_XRC_SRQ_Block(8192);
        mvdev.xrc_srqs->next = NULL;
        curr_block = mvdev.xrc_srqs;
    }
}


struct ibv_qp * MV_Create_XRC_QP(mv_qp_setup_information *si, 
        struct ibv_xrc_domain * xrc_domain) {

    struct ibv_qp * qp = NULL;

    /* create */
    {
        struct ibv_qp_init_attr attr;
        memset(&attr, 0, sizeof(struct ibv_qp_init_attr));

        attr.send_cq = si->send_cq;
        attr.recv_cq = si->recv_cq;

        attr.cap.max_send_wr = si->cap.max_send_wr;
        attr.cap.max_recv_wr = si->cap.max_recv_wr;
        attr.cap.max_send_sge = si->cap.max_send_sge;
        attr.cap.max_recv_sge = si->cap.max_recv_sge;
        attr.cap.max_inline_data = si->cap.max_inline_data;

        attr.xrc_domain = xrc_domain;
        attr.qp_type = IBV_QPT_XRC;
        attr.srq = NULL;

        qp = ibv_create_qp(si->pd, &attr);
        if (!qp) {
            error_abort_all(IBV_RETURN_ERR, "Couldn't create RC QP");
            return NULL;
        }
    }

    /* init */
    {
        struct ibv_qp_attr attr;
        memset(&attr, 0, sizeof(struct ibv_qp_attr));
        attr.qp_state = IBV_QPS_INIT;
        attr.pkey_index = mvparams.pkey_ix;
        attr.port_num = mvparams.default_port;
        attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE |
            IBV_ACCESS_REMOTE_READ;

        attr.pkey_index      = 0;
       

        if(ibv_modify_qp(qp, &attr, 
                    IBV_QP_STATE |
                    IBV_QP_PKEY_INDEX |
                    IBV_QP_PORT |
                    IBV_QP_ACCESS_FLAGS)) {
            error_abort_all(IBV_RETURN_ERR, "Failed to modify RC QP to INIT");
            return NULL;
        }
    }
    mvdev.rc_connections++;

    return qp;
}

mvdev_channel_xrc_shared * MV_Alloc_XRC_Shared(mvdev_connection_t *c) {
    mvdev_channel_xrc_shared *ch = c->xrc_channel.shared_channel;
    mvdev_channel_xrc_shared *new = (mvdev_channel_xrc_shared *)
        malloc(sizeof(mvdev_channel_xrc_shared));

#ifdef MV_PROFILE
    c->msg_info.xrc_xconn++;
#endif

    while(NULL != ch->xtra_xrc_next) {
        ch = ch->xtra_xrc_next;
    }
    ch->xtra_xrc_next = new;
    new->xtra_xrc_next = NULL;

    new->is_xtra = 1;

    return new;
}

mvdev_channel_xrc_shared * MV_Setup_XRC_Shared(mvdev_connection_t *c) {
    int hca = 0;
    mv_qp_setup_information si;

    mvdev_channel_xrc_shared *ch = c->xrc_channel.shared_channel;

    if(MV_XRC_CONN_SHARED_ESTABLISHED == ch->status ||
            MV_XRC_CONN_SHARED_CONNECTING == ch->status) {
        /* we need to allocate a new channel (which will
         * not be used by this node, except to recv)
         */
        ch = MV_Alloc_XRC_Shared(c);
    } 

#ifdef MV_PROFILE
    c->msg_info.xrc_conn++;
#endif

    mv_qp * qp = &(ch->qp[0]);

    si.send_cq = si.recv_cq = mvdev.cq[hca];
    si.cap.max_recv_wr = 0;
    si.srq = NULL;

    si.pd = mvdev.hca[hca].pd;
    si.sq_psn = mvparams.psn;
    si.cap.max_send_sge = 1;
    si.cap.max_recv_sge = 1;
    si.cap.max_send_wr = mvparams.rc_sq_size;

    if(-1 != mvparams.rc_max_inline) {
        si.cap.max_inline_data = mvparams.rc_max_inline;
    } else {
        si.cap.max_inline_data = 0;
    }

    qp->qp = MV_Create_XRC_QP(&si, mvdev.xrc_info->xrc_domain[0]);
    qp->send_wqes_avail = mvparams.rc_sq_size - 5;
    qp->send_wqes_total = mvparams.rc_sq_size - 5;
    qp->ext_sendq_head = qp->ext_sendq_tail = NULL;
    qp->ext_backlogq_head = qp->ext_backlogq_tail = NULL;
    qp->hca = &(mvdev.hca[hca]);
    qp->ext_sendq_size = 0;
    qp->unsignaled_count = 0;
    qp->max_send_size = -1;

    {
        struct ibv_qp_attr attr;
        struct ibv_qp_init_attr init_attr;

        ibv_query_qp(qp->qp, &attr, 0, &init_attr);
        qp->max_inline = init_attr.cap.max_inline_data;
    }

    return ch;
}

void MV_Transition_XRC_QP(struct ibv_qp * qp, uint32_t dest_qpn, 
        uint16_t dest_lid, uint8_t dest_src_path_bits) {
    {
        struct ibv_qp_attr attr;

        dest_src_path_bits = 0;

        memset(&attr, 0, sizeof(struct ibv_qp_attr));
        attr.qp_state = IBV_QPS_RTR;
        attr.path_mtu = mvparams.rc_mtu;

        attr.max_dest_rd_atomic = mvparams.qp_ous_rd_atom;
        attr.min_rnr_timer = mvparams.min_rnr_timer;
        attr.ah_attr.sl = mvparams.service_level;
        attr.ah_attr.port_num = mvparams.default_port;
        attr.ah_attr.static_rate = mvparams.static_rate;

        attr.ah_attr.dlid = dest_lid;
        attr.dest_qp_num = dest_qpn;
        attr.ah_attr.src_path_bits = dest_src_path_bits;

        attr.rq_psn = 0;
        attr.max_dest_rd_atomic = 1;
        attr.min_rnr_timer      = 12;
        attr.ah_attr.is_global  = 0;
        attr.ah_attr.sl         = 0;
        attr.ah_attr.src_path_bits = 0;
        attr.path_mtu           = IBV_MTU_2048;


        D_PRINT("Connecting to qpn: %lu, lid: %d, srcpath: %d\n", dest_qpn,
                dest_lid, dest_src_path_bits);

        if(ibv_modify_qp(qp, &attr, 
                    IBV_QP_STATE |
                    IBV_QP_AV |
                    IBV_QP_PATH_MTU |
                    IBV_QP_DEST_QPN |
                    IBV_QP_RQ_PSN |
                    IBV_QP_MAX_DEST_RD_ATOMIC | 
                    IBV_QP_MIN_RNR_TIMER
                    )) {
            error_abort_all(IBV_RETURN_ERR, "Failed to modify RC QP to RTR");
        }
    }

    /* rts */
    {
        struct ibv_qp_attr attr;
        memset(&attr, 0, sizeof(struct ibv_qp_attr));
        attr.qp_state = IBV_QPS_RTS;
        attr.timeout = mvparams.time_out;
        attr.retry_cnt = mvparams.retry_count;
        attr.rnr_retry = mvparams.rnr_retry;
        attr.sq_psn = mvparams.psn;
        attr.max_rd_atomic = mvparams.qp_ous_rd_atom;

        if(ibv_modify_qp(qp, &attr, 
                    IBV_QP_STATE |
                    IBV_QP_TIMEOUT |
                    IBV_QP_RETRY_CNT |
                    IBV_QP_RNR_RETRY | 
                    IBV_QP_SQ_PSN | 
                    IBV_QP_MAX_QP_RD_ATOMIC
                    )) {
            error_abort_all(IBV_RETURN_ERR, "Failed to modify RC QP to RTR");
        }
    }
}

#endif

