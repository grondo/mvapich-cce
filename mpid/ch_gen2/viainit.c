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

/* Thanks to Mellanox for contributing to the Adaptive RDMA
 * implementation.
 */

#define _USE_MPIRUN_COLLECTIVES_

#define _XOPEN_SOURCE 600

#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>

/* Required for gethostbyname, which is needed for
 * multi-port, multi-hca */
#include <netdb.h>

#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/resource.h>

#include "process/pmgr_collective_client.h"
#include "viadev.h"
#include "viaparam.h"
#include "viapriv.h"
#include "vbuf.h"
#include "dreg.h"
#include "mpid_smpi.h"
#include "ib_init.h"
#include "cm.h"
#include "cm_user.h"
#include "nfr.h"

static void ib_init(void);
static void ib_qp_enable(void);
static void ib_finalize(void);

static int get_host_id(char *myhostname, int hostname_len);

#ifdef _SMP_
extern int smp_num_send_buffer;
extern int smp_batch_size;
#ifdef _AFFINITY_
#include <sched.h>

extern unsigned int viadev_enable_affinity;
#endif /* defined(_AFFINITY_) */

void viainit_setaffinity(char *);
#ifdef _SMP_
void viainit_smpi_init(int *);
#endif
void viainit_init_udcm(void);
void viainit_on_demand_exchange(int);
void viainit_exchange(int);

int *hostnames_od;
#endif /* defined(_SMP_) */

#ifndef DISABLE_PTMALLOC
#include "mem_hooks.h"
#endif /* !DISABLE_PTMALLOC */

#ifdef MCST_SUPPORT
#include <complib/cl_debug.h>
#include "bcast_info.h"

static void ib_ud_qp_init(void);
static void ud_mcst_av_init(void);
static void alloc_ack_region(void);
static void alloc_bcast_buffers(void);
void create_mcgrp(int);
int join_mcgrp(int);
void leave_mcgrp(int);

int mlid;
void viadev_post_ud_recv(vbuf *);
void ud_cleanup(void);
int ud_prepost_count = 0;
int ud_prepost_threshold = VIADEV_UD_PREPOST_THRESHOLD;
int period = PERIOD;
int is_mcast_enabled = 1;

bcast_info_t *bcast_info = &(viadev.bcast_info);
#endif /* MCST_SUPPORT */

#ifdef _SMP_
#ifdef _AFFINITY_
void viainit_setaffinity(char *cpu_mapping) {
    long N_CPUs_online;
    cpu_set_t affinity_mask;
    unsigned long affinity_mask_len = sizeof(affinity_mask);
    char *tp;
    char *cp;
    char tp_str[8];
    int cpu, i, j;

    /*Get number of CPU on machine */
    if ((N_CPUs_online = sysconf(_SC_NPROCESSORS_ONLN)) < 1) {
        perror("sysconf");
    }

    if (cpu_mapping) {
        /* If the user has specified how to map the processes,
         * use the mapping specified by the user
         */
        tp = cpu_mapping;
        j = 0;
        while (*tp != '\0') {
            i = 0;
            cp = tp;
            while (*cp != '\0' && *cp != ',' && *cp != ':') {
                cp++;
                i++;
            }
            strncpy(tp_str, tp, i);
            tp_str[i] = '\0';
            cpu = atoi(tp_str);

            if (j == smpi.my_local_id) {
                CPU_ZERO(&affinity_mask);
                CPU_SET(cpu, &affinity_mask);
                if (sched_setaffinity(0,
                    affinity_mask_len, &affinity_mask)<0 ) {
                    perror("sched_setaffinity");
                }
                break;
            }
            tp = cp + 1;
            j++;
        }

        free(cpu_mapping);
    } else {
        /* The user has not specified how to map the processes,
         * use the default scheme
         */
        CPU_ZERO(&affinity_mask);
        CPU_SET(smpi.my_local_id%N_CPUs_online, &affinity_mask);

        if (sched_setaffinity(0,affinity_mask_len,&affinity_mask)<0 ) {
            perror("sched_setaffinity");
        }
    }
}
#endif /* defined(_AFFINITY_) */
#endif /* defined(_SMP_) */

int vapi_proc_info(int i, char **host, char **exename)
{
    int pid;
    pid = viadev.pids[i];
    *host = viadev.processes[i];
    *exename = viadev.execname;
    return (pid);
}

static void check_attrs(void) 
{
    if(viadev.port_attr.active_mtu < viadev_default_mtu) {
        fprintf(stderr, "Active MTU is %d, VIADEV_DEFAULT_MTU set to %d\n. "
		"See User Guide\n",
                viadev.port_attr.active_mtu, viadev_default_mtu);
    }

    if(viadev.dev_attr.max_qp_rd_atom < viadev_default_qp_ous_rd_atom) {
        error_abort_all(GEN_EXIT_ERR, 
                "Max VIADEV_DEFAULT_QP_OUS_RD_ATOM is %d, set to %d\n",
                viadev.dev_attr.max_qp_rd_atom, viadev_default_qp_ous_rd_atom);
    }

    if(viadev_use_srq) {
        if(viadev.dev_attr.max_srq_sge < viadev_default_max_sg_list) {
            error_abort_all(GEN_EXIT_ERR, 
                    "Max VIADEV_DEFAULT_MAX_SG_LIST is %d, set to %d\n",
                    viadev.dev_attr.max_srq_sge, viadev_default_max_sg_list);
        }

        if(viadev.dev_attr.max_srq_wr < viadev_srq_alloc_size) {
            error_abort_all(GEN_EXIT_ERR, 
                    "Max VIADEV_MAX_SRQ_SIZE is %d, set to %d\n",
                    viadev.dev_attr.max_srq_wr, (int) viadev_srq_alloc_size);
        }
    } else {
        if(viadev.dev_attr.max_sge < viadev_default_max_sg_list) {
            error_abort_all(GEN_EXIT_ERR, 
                    "Max VIADEV_DEFAULT_MAX_SG_LIST is %d, set to %d\n",
                    viadev.dev_attr.max_sge, viadev_default_max_sg_list);
        }

        if(viadev.dev_attr.max_qp_wr < viadev_sq_size) {
            error_abort_all(GEN_EXIT_ERR, 
                    "Max VIADEV_SQ_SIZE is %d, set to %d\n",
                    viadev.dev_attr.max_qp_wr, (int) viadev_sq_size);
        }
    }

    if(viadev.dev_attr.max_cqe < viadev_cq_size) {
        error_abort_all(GEN_EXIT_ERR, 
                "Max VIADEV_CQ_SIZE is %d, set to %d\n",
                viadev.dev_attr.max_cqe, (int) viadev_cq_size);
    }
}

static void get_hca_types(int * num_hcas, int * hca_type) {
    int i, j;
    int total_hcas = 0;
    struct ibv_device **dev_list;
    struct ibv_context *hca_context = NULL;
    struct ibv_port_attr port_attr;

    *num_hcas = 0;
    memset(hca_type, 0, sizeof(int) * MAX_NUM_HCA);

    dev_list = ibv_get_device_list(&total_hcas);

    for (i = 0; i < total_hcas; i++) {
        if (strncmp(viadev.device_name, VIADEV_INVALID_DEVICE, 32) &&
                (strncmp(ibv_get_device_name(dev_list[i]), 
                         viadev.device_name, 32))) {
            continue;
        }

        hca_context = ibv_open_device(dev_list[i]);

        if (!hca_context) {
            error_abort_all(GEN_EXIT_ERR, "Error getting HCA context\n");
        }

        for (j = 1; j <= VIADEV_PORT_MAX_NUM; j++) {
            if (viadev_default_port != -1 && viadev_default_port != j) {
                continue;
            }

            if ((!ibv_query_port(hca_context, j, &port_attr)) &&
                    port_attr.state == IBV_PORT_ACTIVE) {
#ifdef TRANSPORT_RDMAOE_AVAIL
        		if (IBV_LINK_LAYER_ETHERNET == port_attr.link_layer) {
        		    hca_type[*num_hcas] = RDMAOE;
#else
                if (viadev_eth_over_ib) {
        		    hca_type[*num_hcas] = RDMAOE;
#endif /* OFED_1_5_RDMAoE */
        		} else {
                    if (*num_hcas + 1 > MAX_NUM_HCA) {
                        error_abort_all(GEN_EXIT_ERR, "Increase MAX_NUM_HCA -- more than"
                                        " %d HCAs detected\n", MAX_NUM_HCA);
                    }
                    hca_type[*num_hcas] = get_hca_type(dev_list[i], hca_context);
        		}
                (*num_hcas)++;
                break;
            }
        }

        if (ibv_close_device(hca_context)) {
            error_abort_all(GEN_EXIT_ERR, "Error closing HCA\n");
        }

        if (*num_hcas && (!viadev_multihca)) {
            break;
        }
    }

    ibv_free_device_list(dev_list);
}

static void open_ib_port(void)
{
    struct ibv_device **dev_list;
    struct ibv_context *hca_context = NULL;
    struct ibv_port_attr port_attr;
    active_ports *list_active_ports;
    int i = 0, j = 0, num_hcas = 0, num_active_ports = 0;

    dev_list = ibv_get_device_list(&num_hcas);
    list_active_ports = (active_ports *)malloc (num_hcas 
            * VIADEV_PORT_MAX_NUM * sizeof(active_ports));

    if (list_active_ports == NULL) {
        error_abort_all (GEN_EXIT_ERR,
                "malloc for list_active_ports");
    }

    for (j = 0; j < num_hcas; j++) {
        if (strncmp(viadev.device_name, VIADEV_INVALID_DEVICE, 32) &&
                (strncmp(ibv_get_device_name(dev_list[j]), 
                         viadev.device_name, 32))) {
            continue;
        }

        hca_context = ibv_open_device(dev_list[j]);

        if (!hca_context ) {
            error_abort_all(GEN_EXIT_ERR, "Error getting HCA context\n");
        }

        for(i = 1; i <= VIADEV_PORT_MAX_NUM; i++) {
            if (viadev_default_port != -1 
                    && viadev_default_port != i){
                continue;
            }
            
            if ((! ibv_query_port(hca_context,i,&port_attr)) &&
                    port_attr.state == IBV_PORT_ACTIVE &&
                    ((!viadev_eth_over_ib && port_attr.lid) ||
						 viadev_eth_over_ib)) {
                list_active_ports[num_active_ports].hca_id=j;
                list_active_ports[num_active_ports].port_id=i;
                num_active_ports++;
                if (!viadev_multiport){
                    break;
                }
            }
        }

        if(ibv_close_device(hca_context)) {
            error_abort_all(GEN_EXIT_ERR, "Error closing HCA\n");
        }

        if (!viadev_multihca && num_active_ports > 0)
            break;
    }

    if (num_active_ports > 0){
#ifdef _SMP_
        int active_port = (smpi.my_local_id) % num_active_ports;
#else 
        int active_port = (viadev.me) % num_active_ports;
#endif
#ifdef XRC
        viadev_default_hca = list_active_ports[active_port].hca_id;
#endif
        viadev_default_port = list_active_ports[active_port].port_id;
        viadev.nic = dev_list[(list_active_ports[active_port].hca_id)];
        viadev.context = ibv_open_device(viadev.nic);

        if(!viadev.context) {
            error_abort_all(GEN_EXIT_ERR, "Error getting HCA context\n");
        }

        if(ibv_query_device(viadev.context, &viadev.dev_attr)) {
            error_abort_all(GEN_EXIT_ERR,
                    "Error getting HCA attributes\n");
        }   

        viadev.ptag = ibv_alloc_pd(viadev.context);

        if(!viadev.ptag) {
            error_abort_all(GEN_EXIT_ERR, "Error getting protection domain\n");
        }

        if ((!ibv_query_port(viadev.context,
                        viadev_default_port,
                        &port_attr)) &&
                port_attr.state == IBV_PORT_ACTIVE) {

#ifdef TRANSPORT_RDMAOE_AVAIL
            if (IBV_LINK_LAYER_ETHERNET == port_attr.link_layer) {
                /* Enable eth over ib */
                viadev_eth_over_ib = 1;
                /* get guid */
                if (ibv_query_gid(hca_context, viadev_default_port, 0, &viadev.my_hca_id.gid)){
                    error_abort_all(GEN_EXIT_ERR, 
                            "Failed to query MAC on port %d",viadev_default_port);
                }
            } else
#else
            if (viadev_eth_over_ib) {
                /* get guid */
                if (ibv_query_gid(hca_context, viadev_default_port, 0, &viadev.my_hca_id.gid)){
                    error_abort_all(GEN_EXIT_ERR, 
                            "Failed to query MAC on port %d",viadev_default_port);
                }
            } else
#endif
            {
                /* Disable eth over ib */
                viadev.my_hca_id.lid = port_attr.lid;
            }
            viadev.port_attr = port_attr;
            viadev.lmc = port_attr.lmc;
        } else {
            error_abort_all(GEN_EXIT_ERR, 
                    "The port %d changed status",viadev_default_port);
        }
    } else {
        error_abort_all (GEN_EXIT_ERR, "No ACTIVE ports found");
    }

    ibv_free_device_list(dev_list);
    free (list_active_ports);
    D_PRINT("lid=%d\n", viadev.my_hca_id.lid);
}

#ifdef _SMP_
void viainit_smpi_init(int *hostids)
{
    int j;
    char *value;

    /* Run parameter to disable shared memory communication */
    if ( (value = getenv("VIADEV_USE_SHARED_MEM")) != NULL ) {
        disable_shared_mem = !atoi(value);
    }
    /* Should set in KBytes */
    if ( (value = getenv("VIADEV_SMP_EAGERSIZE")) != NULL ) {
        smp_eagersize = atoi(value);
    }
    /* Should set in KBytes */
    if ( (value = getenv("VIADEV_SMPI_LENGTH_QUEUE")) != NULL ) {
        smpi_length_queue = atoi(value);
    }
    if ( (value = getenv("VIADEV_SMP_NUM_SEND_BUFFER")) != NULL ) {
        smp_num_send_buffer = atoi(value);
    }
    if ( (value = getenv("VIADEV_SMP_BATCH_SIZE")) != NULL ) {
        smp_batch_size = atoi(value);
    }

#ifdef _AFFINITY_
    if ((value = getenv("VIADEV_ENABLE_AFFINITY")) != NULL) {
        viadev_enable_affinity = atoi(value);
    }

    /* The new alias will override the old
     * variable, if defined */

    if ((value = getenv("VIADEV_USE_AFFINITY")) != NULL) {
        viadev_enable_affinity = atoi(value);
    }

    if ((value = getenv("VIADEV_CPU_MAPPING")) != NULL) {
        cpu_mapping = (char *)malloc(sizeof(char) * strlen(value) + 1);
        strcpy(cpu_mapping, value);
    }
#endif

    smpi.local_nodes = (unsigned int *)malloc(viadev.np * sizeof(int));
    smpi.l2g_rank = (unsigned int *)malloc(viadev.np * sizeof(int));

    if(smpi.local_nodes == NULL || hostids == NULL) {
        error_abort_all(GEN_EXIT_ERR, "malloc: in viainit_smpi_init");
    }

    smpi.only_one_device = 1;
    smpi.num_local_nodes = 0;

    for (j = 0; j < viadev.np; j++) {
        if (hostids[viadev.me] == hostids[j]) {
            if (j == viadev.me) {
                smpi.my_local_id = smpi.num_local_nodes;
#ifdef _AFFINITY_
	        if (viadev_enable_affinity)
		    viainit_setaffinity(cpu_mapping);
#endif
            }

            smpi.local_nodes[j] = smpi.num_local_nodes;
            smpi.l2g_rank[smpi.num_local_nodes] = j;
            smpi.num_local_nodes++;
        } else {
            smpi.only_one_device = 0;
            smpi.local_nodes[j] = -1;
        }
    }
}
#endif

void viainit_init_udcm(void)
{
    struct ibv_qp_attr attr;
    struct ibv_qp_init_attr init_attr;
    int i;

    viadev.ud_qpn_table = malloc(sizeof(uint32_t) * viadev.np);
    viadev.pending_req_head =
	malloc(sizeof(cm_pending_request *) * viadev.np);
    viadev.pending_req_tail =
	malloc(sizeof(cm_pending_request *) * viadev.np);

    for (i = 0; i < viadev.np; i++) {
	viadev.pending_req_head[i] = NULL;
	viadev.pending_req_tail[i] = NULL;
    }

    /*Setup qp attributes */
    /*Create */
    memset(&init_attr, 0, sizeof(struct ibv_qp_init_attr));
    init_attr.send_cq = viadev.cq_hndl;
    init_attr.recv_cq = viadev.cq_hndl;
    if(viadev_use_srq) {
        init_attr.srq = viadev.srq_hndl;
        init_attr.cap.max_recv_wr = 0;
    } else {
        init_attr.srq = NULL;
        init_attr.cap.max_recv_wr = viadev_rq_size;
    }

    init_attr.cap.max_send_wr = viadev_sq_size;
    init_attr.cap.max_send_sge = viadev_default_max_sg_list;
    init_attr.cap.max_recv_sge = 1;
    init_attr.cap.max_inline_data = viadev_max_inline_size;
    init_attr.qp_type = IBV_QPT_RC;

    cm_ib_qp_attr.rc_qp_init_attr = init_attr;

    /*Init */
    memset(&attr, 0, sizeof(struct ibv_qp_attr));
    attr.qp_state = IBV_QPS_INIT;
    set_pkey_index(&attr.pkey_index,viadev_default_port);
    attr.port_num = viadev_default_port;
    attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE |
	IBV_ACCESS_REMOTE_READ;
    cm_ib_qp_attr.rc_qp_attr_to_init = attr;
    cm_ib_qp_attr.rc_qp_mask_to_init = IBV_QP_STATE |
	IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;

    /*RTR*/
    memset(&attr, 0, sizeof(struct ibv_qp_attr));
    attr.qp_state = IBV_QPS_RTR;
    if(viadev_eth_over_ib) {
        attr.path_mtu = IBV_MTU_1024; /* need to use default MTU size from ifconfig */
    } else {
        attr.path_mtu = viadev_default_mtu;
    }
    attr.max_dest_rd_atomic = viadev_default_qp_ous_rd_atom;
    attr.min_rnr_timer = viadev_default_min_rnr_timer;
    attr.ah_attr.sl = viadev_default_service_level;
    attr.ah_attr.src_path_bits = viadev_default_src_path_bits;
    attr.ah_attr.port_num = viadev_default_port;
    attr.ah_attr.static_rate = viadev_default_static_rate;

    if (viadev_eth_over_ib){
        attr.ah_attr.grh.dgid.global.subnet_prefix = 0;
        attr.ah_attr.grh.dgid.global.interface_id = 0;
        attr.ah_attr.grh.flow_label = 0;
        attr.ah_attr.grh.sgid_index = 0;
        attr.ah_attr.grh.hop_limit = 1;
        attr.ah_attr.grh.traffic_class = 0;
        attr.ah_attr.is_global      = 1;
        attr.ah_attr.dlid           = 0;
    }

    cm_ib_qp_attr.rc_qp_attr_to_rtr = attr;
    cm_ib_qp_attr.rc_qp_mask_to_rtr = IBV_QP_STATE |
	IBV_QP_AV |
	IBV_QP_PATH_MTU |
	IBV_QP_DEST_QPN |
	IBV_QP_RQ_PSN |
	IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;


    /*RTS*/
    memset(&attr, 0, sizeof(struct ibv_qp_attr));
    attr.qp_state = IBV_QPS_RTS;
    attr.timeout = viadev_default_time_out;
    attr.retry_cnt = viadev_default_retry_count;
    attr.rnr_retry = viadev_default_rnr_retry;
    attr.sq_psn = viadev_default_psn;
    attr.max_rd_atomic = viadev_default_qp_ous_rd_atom;

    cm_ib_qp_attr.rc_qp_attr_to_rts = attr;
    cm_ib_qp_attr.rc_qp_mask_to_rts = IBV_QP_STATE |
	IBV_QP_TIMEOUT |
	IBV_QP_RETRY_CNT |
	IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;

    /*Initialize UD-based CM */
    if(MPICM_Init_UD(&(viadev.ud_qpn_table[viadev.me]))) {
	error_abort_all(GEN_EXIT_ERR, "MPICM_Init_UD");
    }
}

void viainit_on_demand_setup()
{                          
    /* ALLOCATING SEND/RECV VBUFS */
    allocate_vbufs(viadev_vbuf_pool_size);

    /* initialize connection parameters */
    viadev.connections = (viadev_connection_t *)
	malloc(sizeof(viadev_connection_t) * viadev.np);

    if (NULL == viadev.connections) {
	error_abort_all(GEN_EXIT_ERR,
		"Unable to malloc connection structures");
    }

    memset(viadev.connections, 0,
	    (sizeof(viadev_connection_t) * viadev.np));

    /* Initialize the NULL values for connection to itself*/
    /* these should be ignored for self-communication */
    viadev.connections[viadev.me].vi = NULL;
    viadev.connections[viadev.me].remote_credit = 0;
    viadev.connections[viadev.me].remote_cc = 0;
    viadev.connections[viadev.me].local_credit = 0;

#ifdef XRC
    if (viadev_use_xrc) {
        int i;
        for (i = 0; i < viadev.np; i++) {
            viadev.qp_hndl[i] = NULL;
            viadev_connection_t *c = &viadev.connections[i];
#ifdef _SMP_
            if (viadev.hostids[viadev.me] != viadev.hostids[i] 
                    || disable_shared_mem) 
#endif
            {
                c->next_packet_expected = 1;
                c->next_packet_tosend = 1;
                c->initialized = 1;
                c->global_rank = i;
                c->xrc_conn_queue = NULL;
            }
        }
    }
#endif 

#if defined(ADAPTIVE_RDMA_FAST_PATH)
    viadev.RDMA_polling_group_size = 0;
    viadev.RDMA_polling_group = (viadev_connection_t **)
	malloc(viadev.np * sizeof(viadev_connection_t *));

    if (NULL == viadev.RDMA_polling_group) {
	error_abort_all(GEN_EXIT_ERR,
		"Unable to malloc connection structures");
    }
#endif
    if(viadev_use_srq) {
	pthread_spin_init(&viadev.srq_post_spin_lock, 0);

	pthread_mutex_init(&viadev.srq_post_mutex_lock, 0);
	pthread_cond_init(&viadev.srq_post_cond, 0);

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
    pthread_create(&viadev.async_thread, NULL,
	    (void *) async_thread, (void *) viadev.context);
}

void viainit_on_demand_exchange(int nprocs)
{
    typedef struct {
        pid_t	pid;
        lgid    hca_id;
        int	ud_qpn;
    } od_exchange_info;

    od_exchange_info my_info, all_info[nprocs];
    int i;

    my_info.pid	    = getpid();
    my_info.hca_id  = viadev.my_hca_id;
    my_info.ud_qpn  = viadev.ud_qpn_table[viadev.me];

    pmgr_allgather(&my_info, sizeof(my_info), all_info);

    for (i = 0; i < viadev.np; i++) {
        viadev.pids[i] = all_info[i].pid;

        viadev.lgid_table[i] = all_info[i].hca_id;

        if(i != viadev.me) {
            viadev.ud_qpn_table[i] = all_info[i].ud_qpn;
        }
    }

    if (MPICM_Connect_UD(viadev.ud_qpn_table, viadev.lgid_table)) {
        error_abort_all(GEN_EXIT_ERR, "MPICM_Connect_UD");
    }
}

void viainit_exchange(int nprocs)
{
    int qp_list[nprocs], i, pid;

    for (i = 0; i < nprocs; ++i) {
        if (i == viadev.me) {
            qp_list[i] = viadev.my_hca_id.lid;
        } else {
            qp_list[i] = viadev.qp_hndl[i]->qp_num;
        }
    }

    pid = (int) getpid();

    pmgr_alltoall(qp_list, sizeof(int), viadev.qp_table);
    pmgr_allgather(&viadev.my_hca_id, sizeof(viadev.my_hca_id),
            viadev.lgid_table);
    pmgr_allgather(&pid, sizeof(pid), viadev.pids);
}

int MPID_VIA_Init(int *argc, char ***argv, int *size, int *rank)
{
    int i, j, is_homogeneous, hostid = 0, mypid;
    int *alladdrs = NULL, *local_addr = NULL, *allhostids = NULL;
    int hca_num;
    int hca_types[MAX_NUM_HCA];

#ifdef MCST_SUPPORT
    char grp_info[GRP_INFO_LEN];

    if (NULL != getenv("DISABLE_HARDWARE_MCST")) {
        /* The user has disabled Multicast */
        is_mcast_enabled = 0;
    } else {
        /* The user has enabled Multicast */
        is_mcast_enabled = 1;
    }
#endif

#ifdef MEMORY_RELIABLE
    gen_crc_table();
    init_crc_queues();
    init_ofo_queues();
#endif

    /* init the VIA barrier counter */
    viadev.barrier_id = 0;

    /* Device is not yet initialized */
    viadev.initialized = 0;

    /*
     * Find out from the process manager who we are. 
     */
    if(pmgr_init(argc, argv, &viadev.np, &viadev.me, &viadev.global_id) !=
	    PMGR_SUCCESS) {
        error_abort_all(GEN_EXIT_ERR, "pmgr_init_failed");
    }

    pmgr_open();
    viadev.processes_buffer = NULL;
    viadev.processes = NULL;
    char host[256];
    gethostname(host, sizeof(host));
    pmgr_allgatherstr(host, &viadev.processes, &viadev.processes_buffer);

    *size = viadev.np;
    *rank = viadev.me;

    /* for IB systems, increase our memlock limit */
    struct rlimit rl;
    getrlimit(RLIMIT_MEMLOCK, &rl);
    rl.rlim_cur = rl.rlim_max;
    setrlimit(RLIMIT_MEMLOCK, &rl);

    if(NULL != argv) {
        strncpy(viadev.execname, 
                ((const char **) (*argv))[0], VIADEV_MAX_EXECNAME);
    } else {
        strncpy(viadev.execname, "unknown_exe", VIADEV_MAX_EXECNAME);
    }

    viadev.is_finalized = 0;

    init_vbuf_lock();

    /* Now that we know our rank, we should also know
     * our name */
    viadev.my_name = (char *) malloc(sizeof(char) * HOSTNAME_LEN);

    if (NULL == viadev.my_name) {
        error_abort_all(GEN_EXIT_ERR, "malloc failed");
    }

    if (gethostname(viadev.my_name, sizeof(char) * HOSTNAME_LEN) < 0) {
        error_abort_all(GEN_EXIT_ERR, "gethostname failed");
    }

    D_PRINT("size=%d, myrank=%d\n", *size, *rank);


    /* For both SMP and no-SMP, we have the same address length */
    alladdrs = (int *) malloc(3 * viadev.np * sizeof(int));
    local_addr = (int *) malloc(2 * viadev.np * sizeof(int));

    viadev.lgid_table = (lgid *) calloc(viadev.np ,sizeof(lgid));
    viadev.qp_table = (uint32_t *) calloc(viadev.np ,sizeof(int));
    viadev.qp_hndl =
        (struct ibv_qp **) calloc(viadev.np ,sizeof(struct ibv_qp *));
    viadev.pids = (int *) calloc(viadev.np ,sizeof(int));
    viadev.pids[viadev.me] = mypid = (int) getpid();

    if (alladdrs == NULL || local_addr == NULL ||
        viadev.qp_table == NULL || viadev.lgid_table == NULL) {
        error_abort_all(GEN_EXIT_ERR,
                        "malloc for alladdrs/local_addr/lid_table/qp_table");
    }

    hostid = get_host_id(viadev.my_name, HOSTNAME_LEN);
	
    allhostids= (int *)malloc(viadev.np * sizeof(hostid));
    if (hostid == 0 || allhostids == NULL) {
        error_abort_all(GEN_EXIT_ERR,
                "malloc: in init");
    }

    viadev_init_hca_parameters();
    get_hca_types(&hca_num, hca_types);

    {
        typedef struct {
            int host_id;
            int hca_count;
            int hca_type[MAX_NUM_HCA];
        } via_host_info;

        via_host_info local_host_info, *remote_host_info;

        remote_host_info = (via_host_info *) 
            malloc(sizeof(via_host_info) * viadev.np);
        local_host_info.host_id = hostid;
        local_host_info.hca_count = hca_num;
        for (i = 0; i < hca_num; i++) {
            local_host_info.hca_type[i] = hca_types[i];
        }

        pmgr_allgather(&local_host_info, sizeof(local_host_info), remote_host_info);

        is_homogeneous = 1;
        for (i = 0; i < viadev.np; i++) {
            allhostids[i] = remote_host_info[i].host_id;
            for (j = 0; j < remote_host_info[i].hca_count; j++) {
                if (remote_host_info[i].hca_type[j] != hca_types[0]) {
                    is_homogeneous = 0;
		            if (RDMAOE == hca_types[0] || 
                        RDMAOE == remote_host_info[i].hca_type[j]) {
		                    error_abort_all (IBV_RETURN_ERR,
                            "Mvapich does not supports RDMAOE and IB networks for the same run\n");
                    }
                    break;
                }
            }
        }

        free(remote_host_info);
    }
#ifdef _SMP_
    viainit_smpi_init(allhostids);
#endif

    open_ib_port();

#ifdef XRC
    viadev.hostids = allhostids;
#else 
    free(allhostids);
#endif /* ifdef XRC */

    /* set default parameters */
    viadev_set_default_parameters(viadev.np, viadev.me, is_homogeneous, hca_types[0]);

    /* set run-time parameters that have been passed in through
     * environment variables */
    viadev_init_parameters(viadev.np, viadev.me);
    nfr_init();

    viadev.num_connections = viadev_use_on_demand ? 0 : viadev.np;
    viadev.maxtransfersize = viadev_max_rdma_size;

#ifdef DISABLE_PTMALLOC
    /* Set glibc/stdlib malloc options to prevent handing
     * memory back to the system (brk) upon free.
     * Also, dont allow MMAP memory for large allocations.
     * This because we dont unregister memory when refcounts
     * go to zero.
     * Only need these options when RDMA operations are being
     * used.  Otherwise, MVICH layer owns all registered memory
     * and we will only free it if it is de-registered anyway.
     */
    if(viadev_use_dreg_cache) {
        mallopt(M_TRIM_THRESHOLD, -1);
        mallopt(M_MMAP_MAX, 0);
    }
#else
    if(mvapich_minit()) {
        /* Not able to find expected malloc library,
         * disable registration cache to be safe */
        viadev_use_dreg_cache = 0;
    }
#endif                          /* DISABLE_PTMALLOC */

    check_attrs();

    /* Since we need to excahge qp # also, 
     * we will create QP first, exchange addresses and 
     * then enable them */

    ib_init();

    /* Initialize the lock for APM, if usage is defined */
    if(viadev_use_apm) {
        init_apm_lock();
    }

    viadev.array_recv_desc = (struct ibv_recv_wr *)
        malloc(sizeof(struct ibv_recv_wr) * viadev_rq_size);

    memset(viadev.array_recv_desc, 0,
           sizeof(struct ibv_recv_wr) * viadev_rq_size);

    /*ON_DEMAND*/
    if(viadev_use_on_demand) {
	viainit_init_udcm();
    }

#ifdef MCST_SUPPORT
    if (is_mcast_enabled) {
        viadev.mcg_dgid.raw[0] = 0xFF;
        viadev.mcg_dgid.raw[1] = 0x00;
        viadev.mcg_dgid.raw[2] = 0x00;
        viadev.mcg_dgid.raw[3] = 0x00;
        viadev.mcg_dgid.raw[4] = 0x00;
        viadev.mcg_dgid.raw[5] = 0x00;
        viadev.mcg_dgid.raw[6] = 0x00;
        viadev.mcg_dgid.raw[7] = 0x00;
        viadev.mcg_dgid.raw[8] = 0x00;
        viadev.mcg_dgid.raw[9] = 0x00;
        viadev.mcg_dgid.raw[10] = 0x00;
        viadev.mcg_dgid.raw[11] = 0x00;
        viadev.mcg_dgid.raw[12] = 0x00;
        viadev.mcg_dgid.raw[13] = 0x00;
        viadev.mcg_dgid.raw[14] = 0x00;
        viadev.mcg_dgid.raw[15] = 0x00;

        ib_ud_qp_init();        /* Allocate UD-QP */

#if defined(MINISM)
        ud_mcst_av_init();      /* Allocate address vect.for  multicast */
#endif

        alloc_ack_region();     /* Allocate Ack region for reliability  */
        alloc_bcast_buffers();  /* Allocate Bcast,Reliability structures  */
    }
#endif /* defined(MCST_SUPPORT) */

    if(viadev_use_on_demand) {
        viainit_on_demand_setup();
        viainit_on_demand_exchange(viadev.np);
        pmgr_barrier();
    }
    else { /* not use on demand */
        viainit_exchange(viadev.np);
        ib_qp_enable();
    }

    free(alladdrs);

    if (viadev_async_progress) {
        if(viadev_init_async_progress()) {
            error_abort_all(GEN_EXIT_ERR, "Initialization of  async progress \
failed. It might be that the user does not have privileges for the set \
schedule policy.\n");
        }
    }

    dreg_init();

#if defined(MCST_SUPPORT)
    if (is_mcast_enabled) {
        if (viadev.me == 0) {
            create_mcgrp(viadev_default_port);  /* Creation of mcst group */
        }

        if (viadev.me == 0) {
            strncpy(grp_info, "MCS_Grp1", GRP_INFO_LEN);
        }
    }

    pmgr_bcast(grp_info, GRP_INFO_LEN, 0);
    pmgr_barrier();

    if (is_mcast_enabled) {

        mlid = join_mcgrp(viadev_default_port); /* Joining the group */

        ud_mcst_av_init();      /* Allocate address vect. for multicast */

        /* Preposting the descriptors for the UD transport */
        for (i = 0; i < VIADEV_UD_PREPOST_DEPTH; i++) {
            PREPOST_UD_VBUF_RECV();
            ud_prepost_count++;
        }
    }
#endif /* defined(MCST_SUPPORT) */

    viadev.coll_comm_reg = NULL;

    pmgr_close();

    return 0;
}

/*
 * Set up all communication and put VIs in the VI array in viadev
 * The algorithm is as follows (may change when M-VIA supports peer-to-peer
 * connections):
 *   . use client-server routines to establish connections
 *   . for each pair, the lower-numbered process acts as the server
 *   . we should be careful not to establish hotspots. E.g., if
 *     every process connects with others in order 0, 1, 2. We
 *     should also be careful not to serialize. 
 *     there is a nice fancy way to do this, with a communication pattern
 *     essentially the same as in an FFT. 
 *   . However, it is a bit tricky to get right so for now 
 *     do the stupid serialized version (xxx):
 *        for (i = 0; i < me; i++) connect(i)
 *        for (i = me+1; i <= np; i++) accept(i)
 *   
 *  Note that we do not set up a connection to ourselves 
 *
 */

void MPID_Wtime(double *t)
{
    struct timeval tv;
    static int initialized = 0;
    static int sec_base;
    gettimeofday(&tv, NULL);
    if (!initialized) {
        sec_base = tv.tv_sec;
        initialized = 1;
    }
    *t = (double) (tv.tv_sec - sec_base) + (double) tv.tv_usec * 1.0e-6;
}

void MPID_Wtick(double *t)
{
    *t = VIADEV_WTICK;
}

void MPID_VIA_Finalize(void)
{
    /* Unpinning any leftover collective buffers */
    reg_entry *current = viadev.coll_comm_reg;
    reg_entry *tmp;

#ifndef DISABLE_PTMALLOC
    mvapich_mfin();
#endif

    while (current != NULL) {
        tmp = current;
        if (current->valid == 1) {
            deregister_memory(current->mem_hndl);
        }
        current = current->next;
        free(tmp);
    }

    /* VAPI finalize called */
    ib_finalize();

    /* free connection array space */
    nfr_finalize();
    if (viadev.connections) {
        free(viadev.connections);
    }

    /* let the process manager reclaim data structures 
     * before termination */

    pmgr_finalize();

    if (viadev.lgid_table) {
        free(viadev.lgid_table);
    }
    if (viadev.qp_table) {
        free(viadev.qp_table);
    }
    if (viadev.pids) {
        free(viadev.pids);
    }
    if (viadev.processes) {
        free(viadev.processes);
    }
    if (viadev.processes_buffer) {
        free(viadev.processes_buffer);
    }
#ifdef _SMP_
    if(!disable_shared_mem) {
        if (smpi.local_nodes) {
            free(smpi.local_nodes);
        }
        if (smpi.l2g_rank) {
            free(smpi.l2g_rank);
        }
    }
#endif

#ifdef MCST_SUPPORT
    /* complib_exit() added for fixing comlib issue on RH-AS3,RH90,SLES9.
       Works only with osm-mvapi-0.3.2-rc1 */
    complib_exit();
#endif


    D_PRINT("exiting MPID_VIA_Finalize...\n");

}

#ifdef XRC
static void xrc_init (void) 
{
    char xrc_file[256+26+22+3];
    /* 256:  Max hostname len, 26: exec name, 22: UID, 3: HCA */

    if (!(viadev.dev_attr.device_cap_flags & IBV_DEVICE_XRC)) {
        error_abort_all (IBV_RETURN_ERR, "Device doesn't support XRC\n");
    }

    viadev.xrc_info = (xrc_info_t *) malloc (xrc_info_s);
    memset (viadev.xrc_info, 0, xrc_info_s);
    /* Join XRC domains */
    sprintf (xrc_file, "/dev/shm/mvapich-xrc-%d-%s-%d-%d.tmp", 
            viadev.global_id, viadev.my_name, getuid (), viadev_default_hca);

    viadev.xrc_info->fd = open (xrc_file, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);

    if (viadev.xrc_info->fd < 0) {
        perror ("xrc_init");
        fprintf (stderr, "Error creating XRC file: %s\n", xrc_file);
    }
    viadev.xrc_info->xrc_domain = ibv_open_xrc_domain (viadev.context,
            viadev.xrc_info->fd, O_CREAT);
    
    if (viadev.xrc_info->xrc_domain == 0) {
        error_abort_all (IBV_RETURN_ERR, "Error opening XRC domain\n");
    }
}
#endif /* XRC */

/* internal functions used for initialization 
 * and finalization by VAPI */

static void ib_init(void)
{
    int i;
    struct ibv_qp_attr qp_attr;
    struct ibv_qp_init_attr attr;

    if(viadev_use_blocking) {
        viadev.comp_channel = ibv_create_comp_channel(viadev.context);

        if (!viadev.comp_channel) {
            error_abort_all(IBV_RETURN_ERR,
                    "Cannot create completion channel\n");
        }

        viadev.cq_hndl = create_cq(viadev.comp_channel);

        if (ibv_req_notify_cq(viadev.cq_hndl, 0)) {
            error_abort_all(IBV_RETURN_ERR,
                    "Cannot request completion notification\n");
        }
    } else if (viadev_async_progress) {
        viadev.comp_channel = ibv_create_comp_channel(viadev.context);

        if (!viadev.comp_channel) {
            error_abort_all(IBV_RETURN_ERR,
                    "Cannot create completion channel\n");
        }

        viadev.cq_hndl = create_cq(viadev.comp_channel);
    }
    else {
        viadev.cq_hndl = create_cq(NULL);
    }
#ifdef XRC
    if (viadev_use_xrc) {
        /* Initialize XRC */
        xrc_init ();
        if (viadev_multihca) {
            viadev.def_hcas = (unsigned int *) malloc (viadev.np * 
                    sizeof (unsigned int));
            pmgr_allgather (&(viadev_default_hca), 
                    sizeof (viadev_default_hca), viadev.def_hcas);
        }
    }
#endif 
    if(viadev_use_srq) {
        viadev.srq_hndl = create_srq();
    }

  if (!viadev_use_on_demand)
  {
    for (i = 0; i < viadev.np; i++) {

        memset(&attr, 0, sizeof(attr));
        attr.send_cq = viadev.cq_hndl;
        attr.recv_cq = viadev.cq_hndl;
        if(viadev_use_srq) {
            attr.srq = viadev.srq_hndl;
            attr.cap.max_recv_wr = 0;
        } else {
            attr.cap.max_recv_wr = viadev_rq_size;
        }
        attr.cap.max_send_wr = viadev_sq_size;
        attr.cap.max_send_sge = viadev_default_max_sg_list;
        attr.cap.max_recv_sge = 1;
#ifndef _IBM_EHCA_
        attr.cap.max_inline_data = viadev_max_inline_size;
#else
        attr.cap.max_inline_data = -1;
#endif
        attr.qp_type = IBV_QPT_RC;
        if (i == viadev.me) {
            continue;
        }

        viadev.qp_hndl[i] = ibv_create_qp(viadev.ptag, &attr);

        if (!viadev.qp_hndl[i]) {
            perror("QP failed");
            error_abort_all(GEN_EXIT_ERR, "Error creating QP\n");
        }
    }

    for (i = 0; i < viadev.np; i++) {

        if (i == viadev.me) {
            continue;
        }

        qp_attr.qp_state = IBV_QPS_INIT;
        set_pkey_index(&qp_attr.pkey_index,viadev_default_port); 
        qp_attr.port_num = viadev_default_port;
        qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE |
            IBV_ACCESS_REMOTE_READ;

        if (ibv_modify_qp(viadev.qp_hndl[i], &qp_attr,
                          IBV_QP_STATE |
                          IBV_QP_PKEY_INDEX |
                          IBV_QP_PORT | IBV_QP_ACCESS_FLAGS)) {
            error_abort_all(GEN_EXIT_ERR, "Couldn't modify QP to init\n");
        }
    }
  }
}


static void ib_qp_enable(void)
{
    int i, j;
    struct ibv_qp_attr attr;

#ifdef ADAPTIVE_RDMA_FAST_PATH
    int pagesize;
    int vbuf_alignment;
#endif

    for (i = 0; i < viadev.np; i++) {

        memset(&attr, 0, sizeof(attr));

        attr.qp_state = IBV_QPS_RTR;

        if(viadev_eth_over_ib) {
            attr.path_mtu = IBV_MTU_1024; /* need to use default MTU size from ifconfig */
        } else {
            attr.path_mtu = viadev_default_mtu;
        }

        attr.max_dest_rd_atomic = viadev_default_qp_ous_rd_atom;
        attr.min_rnr_timer = viadev_default_min_rnr_timer;
        attr.ah_attr.sl = viadev_default_service_level;
        attr.ah_attr.port_num = viadev_default_port;
        attr.ah_attr.static_rate = viadev_default_static_rate;
        if (viadev_eth_over_ib){
                attr.ah_attr.grh.dgid.global.subnet_prefix = 0;
                attr.ah_attr.grh.dgid.global.interface_id = 0;
                attr.ah_attr.grh.flow_label = 0;
                attr.ah_attr.grh.sgid_index = 0;
                attr.ah_attr.grh.hop_limit = 1;
                attr.ah_attr.grh.traffic_class = 0;
                attr.ah_attr.is_global      = 1;
                attr.ah_attr.dlid           = 0;
        }

        if (i == viadev.me) {
            continue;
        }
        attr.dest_qp_num = viadev.qp_table[i];
        if(!disable_lmc) {
            attr.ah_attr.dlid = viadev.lgid_table[i].lid + 
                (viadev.me + i) % (power_two(viadev.lmc));
            attr.ah_attr.src_path_bits = 
                viadev_default_src_path_bits + (viadev.me + i) 
                % (power_two(viadev.lmc));
        } else {
            if (viadev_eth_over_ib){
                attr.ah_attr.grh.dgid = viadev.lgid_table[i].gid;
            } else {
                attr.ah_attr.dlid = viadev.lgid_table[i].lid;
            }
            attr.ah_attr.src_path_bits =
                viadev_default_src_path_bits;
        }
        if (ibv_modify_qp(viadev.qp_hndl[i], &attr,
                          IBV_QP_STATE |
                          IBV_QP_AV |
                          IBV_QP_PATH_MTU |
                          IBV_QP_DEST_QPN |
                          IBV_QP_RQ_PSN |
                          IBV_QP_MAX_DEST_RD_ATOMIC |
                          IBV_QP_MIN_RNR_TIMER)) {
            error_abort_all(GEN_EXIT_ERR, "Failed to modify QP to RTR\n");
        }
    }

    for (i = 0; i < viadev.np; i++) {

        memset(&attr, 0, sizeof(attr));

        attr.qp_state = IBV_QPS_RTS;
        attr.timeout = viadev_default_time_out;
        attr.retry_cnt = viadev_default_retry_count;
        attr.rnr_retry = viadev_default_rnr_retry;
        attr.sq_psn = viadev_default_psn;
        attr.max_rd_atomic = viadev_default_qp_ous_rd_atom;

        if (i == viadev.me) {
            continue;
        }

        if (ibv_modify_qp(viadev.qp_hndl[i], &attr,
                          IBV_QP_STATE |
                          IBV_QP_TIMEOUT |
                          IBV_QP_RETRY_CNT |
                          IBV_QP_RNR_RETRY |
                          IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC)) {
            error_abort_all(GEN_EXIT_ERR, "Failed to modify QP to RTS\n");
        }
  
        /* If APM is defined, load an alternate path for the QP */

        if(viadev_use_apm) { 
            reload_alternate_path(viadev.qp_hndl[i]);
        }
    }


    /****************prepost receive descriptors ****************/

    /* ALLOCATING SEND/RECV VBUFS */
    allocate_vbufs(viadev_vbuf_pool_size);

    /* initialize connection parameters */
    viadev.connections = (viadev_connection_t *)
        malloc(sizeof(viadev_connection_t) * viadev.np);

    if (NULL == viadev.connections) {
        error_abort_all(GEN_EXIT_ERR,
                        "Unable to malloc connection structures");
    }

    memset(viadev.connections, 0,
           (sizeof(viadev_connection_t) * viadev.np));

#if defined(ADAPTIVE_RDMA_FAST_PATH)
    viadev.RDMA_polling_group_size = 0;
    viadev.RDMA_polling_group = (viadev_connection_t **)
        malloc(viadev.np * sizeof(viadev_connection_t *));

    if (NULL == viadev.RDMA_polling_group) {
        error_abort_all(GEN_EXIT_ERR,
                        "Unable to malloc connection structures");
    }
#endif

    if(viadev_use_srq) {
        pthread_spin_init(&viadev.srq_post_spin_lock, 0);

        pthread_mutex_init(&viadev.srq_post_mutex_lock, 0);
        pthread_cond_init(&viadev.srq_post_cond, 0);

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

    /* Start the async thread which watches for SRQ limit events */
    pthread_create(&viadev.async_thread, NULL,
            (void *) async_thread, (void *) viadev.context);

    for (i = 0; i < viadev.np; i++) {

        viadev_connection_t *c = &viadev.connections[i];
        c->max_inline = viadev_max_inline_size;
        c->global_rank = i;
        c->next_packet_expected = 1;
        c->next_packet_tosend = 1;
        c->shandle_head = NULL;
        c->shandle_tail = NULL;
        c->rhandle = NULL;
        c->nextflow = NULL;
        c->inflow = 0;
        c->preposts = 0;
        c->send_wqes_avail = viadev_sq_size;
        c->ext_sendq_size = 0;
        c->ext_sendq_head = NULL;
        c->ext_sendq_tail = NULL;
        c->rendezvous_packets_expected = 0;
        viadev_init_backlog_queue(c);

        c->coalesce_cached_out.context = -1;
        c->coalesce_cached_in.context = -1;

        c->rdma_reads_avail = viadev_default_qp_ous_rd_atom;
        c->ext_rdma_read_head = NULL;
        c->ext_rdma_read_tail = NULL;

#ifdef ADAPTIVE_RDMA_FAST_PATH
#ifndef DISABLE_HEADER_CACHING
        /* init cached packet header */
        memset(&(c->cached_outgoing), 0,
               sizeof(viadev_packet_eager_start));
        memset(&(c->cached_incoming), 0,
               sizeof(viadev_packet_eager_start));
#ifdef VIADEV_DEBUG
        c->cached_hit = c->cached_miss = 0;
#endif
        c->cached_incoming.header.type = c->cached_outgoing.header.type
            = FAST_EAGER_CACHED;
#endif
#endif
        if (viadev_use_nfr) {
            WAITING_LIST_INIT(&c->waiting_for_ack);
            WAITING_LIST_INIT(&c->rndv_inprocess);
        }
        c->pending_acks = 0;
        c->was_connected = 0;
        c->qp_status = QP_UP;
        c->progress_recov_mode = 0;

        if (i != viadev.me) {
            c->vi = viadev.qp_hndl[i]; 

            if(!viadev_use_srq) {
                /* prepost receives */
                for (j = 0; j < viadev_initial_prepost_depth; j++) {
                    PREPOST_VBUF_RECV(c);
                }
            }

            c->remote_credit = viadev_initial_credits;
            c->remote_cc = viadev_initial_credits;
            c->local_credit = 0;
            c->preposts = viadev_initial_prepost_depth;

            c->pending_r3_data = 0;
            c->received_r3_data = 0;

            if(viadev_use_srq) {
                c->initialized = 1;
            } else {
                c->initialized = (viadev_initial_prepost_depth
                        ==
                        viadev_prepost_depth +
                        viadev_prepost_noop_extra);
            }
        } else {
            /* these should be ignored for self-communication */
            c->vi = NULL;
            c->remote_credit = 0;
            c->remote_cc = 0;
            c->local_credit = 0;
        }

#ifdef ADAPTIVE_RDMA_FAST_PATH
        vbuf_alignment = 64;
        pagesize = getpagesize();

        if (i != viadev.me) {

            /* initialize revelant fields */
            c->rdma_credit = 0;
            c->num_no_completion = 0;

            if (viadev_num_rdma_buffer) {

                /* By default, all pointers are cleared, indicating no buffers
                 * at the receive and no credits at the sender side. Only if 
                 * RDMA eager buffers are allocated, this pointers will be updated
                 */
                c->phead_RDMA_send = 0;
                c->ptail_RDMA_send = 0;
                c->p_RDMA_recv = 0;
                c->p_RDMA_recv_tail = 0;

            } else {
                c->phead_RDMA_send = 0;
                c->ptail_RDMA_send = 0;
                c->p_RDMA_recv = 0;
                c->p_RDMA_recv_tail = 0;
            }
            /* remote address and handle not available yet */
            c->remote_address_received = 0;

        }
#endif                          /* RDMA_FAST_PATH */

    }

#if defined(MCST_SUPPORT) && defined(MINISM)
    if (is_mcast_enabled) {
        /* Preposting the descriptors for the UD transport */
        for (i = 0; i < VIADEV_UD_PREPOST_DEPTH; i++) {
            PREPOST_UD_VBUF_RECV();
            ud_prepost_count++;
        }
    }
#endif                          /* MCST_SUPPORT && MINISM */

    /* call barrier to make sure all preposting has been done */
    D_PRINT("about to call pmgr_barrier...\n");

    /* call barrier to make sure all preposting has been done 
     * to gurantee all have preposted requests before real comm.
     * With MPD support, we use the BOOTSTRAP_CHANNEL,
     * which makes it simple to ensure the correctness of 
     * this barrier w.r.t to other traffic */
    i = pmgr_barrier();

    D_PRINT("process manager barrier returns %d\n", i);

    if (viadev_num_rdma_buffer == 0)
        return;

#ifdef MCST_SUPPORT
    /* inform others about the receive 
     * RDMA buffer address and handle */

    for (i = 0; i < viadev.np; i++) {
        vbuf *v;
        viadev_packet_rdma_address *h;
        viadev_connection_t *c = &viadev.connections[i];

        if (i == viadev.me) {
            continue;
        }

        v = get_vbuf();
        h = (viadev_packet_rdma_address *) VBUF_BUFFER_START(v);

        /* set up the packet */
        PACKET_SET_HEADER_NR(h, c, VIADEV_RDMA_ADDRESS);
        h->envelope.src_lrank = i;

#if defined(RDMA_FAST_PATH)
        /* set address and handle */
        h->RDMA_address = c->RDMA_recv_buf_DMA;
        h->RDMA_hndl = c->RDMA_recv_buf_hndl->rkey;
#endif                          /* RDMA_FAST_PATH */

#ifdef MCST_SUPPORT
        if (is_mcast_enabled) {
            /* set address and handle for UD Acks, this is for now remove later */
            h->RDMA_ACK_address = (viadev.bcast_info.ack_buffer);
            h->RDMA_ACK_hndl = viadev.bcast_info.ack_mem_hndl->rkey;
        }
#endif                          /* MCST_SUPPORT */

        /* finally send it off */
        vbuf_init_send(v, sizeof(viadev_packet_rdma_address));

        viadev_post_send(c, v);
    }

    /* get all the address */
    for (i = 0; i < viadev.np; i++) {
        viadev_connection_t *c = &viadev.connections[i];

        if (i == viadev.me) {
            continue;
        }

        while (!c->remote_address_received) {
            MPID_DeviceCheck(1);
        }
    }

#endif                          /* RDMA_FAST_PATH || MCST_SUPPORT */

}

#ifdef ADAPTIVE_RDMA_FAST_PATH
void process_remote_rdma_address(viadev_connection_t * c,
                                 uint32_t remote_hndl,
                                 struct vbuf *remote_address);

void process_remote_rdma_address(viadev_connection_t * c,
                                 uint32_t remote_hndl,
                                 struct vbuf *remote_address)
{
    int i;

    c->remote_address_received = 1;

    if (remote_address != 0) {

        /* Allocating the send vbufs for the eager RDMA flow */
        vbuf_fast_rdma_alloc(c, 0);
        c->remote_RDMA_buf_hndl = remote_hndl;
        c->remote_RDMA_buf = remote_address;

        for (i = 0; i < viadev_num_rdma_buffer; i++) {

            /* fill out the descriptors */

            /* this field to be filled out later */
            c->RDMA_send_buf[i].desc.sg_entry.length = 0;

            c->RDMA_send_buf[i].desc.sg_entry.addr =
                (uintptr_t) (c->RDMA_send_buf[i].head_flag);

            c->RDMA_send_buf[i].desc.sg_entry.lkey =
                c->RDMA_send_buf_hndl->lkey;

            c->RDMA_send_buf[i].desc.u.sr.sg_list =
                &(c->RDMA_send_buf[i].desc.sg_entry);

            c->RDMA_send_buf[i].padding = FREE_FLAG;

            c->RDMA_send_buf[i].desc.u.sr.send_flags = IBV_SEND_SIGNALED;
            c->RDMA_send_buf[i].desc.u.sr.opcode = IBV_WR_RDMA_WRITE;
            c->RDMA_send_buf[i].desc.u.sr.wr_id =
                (aint_t) & (c->RDMA_send_buf[i]);
            c->RDMA_send_buf[i].desc.u.sr.wr.rdma.rkey =
                c->remote_RDMA_buf_hndl;
            c->RDMA_send_buf[i].desc.u.sr.wr.rdma.remote_addr =
                (uintptr_t) ((char *) (c->remote_RDMA_buf) +
                             (i * viadev_vbuf_total_size));
            c->RDMA_send_buf[i].desc.u.sr.num_sge = 1;

        }
    }

}

#endif

static void ib_finalize()
{
    int i;
#ifdef MCST_SUPPORT

    if (is_mcast_enabled) {
        ud_cleanup();
    }
#ifdef _SMP_
    if (smpi.my_local_id == 0) {
        leave_mcgrp(viadev_default_port);
    }
#else
    leave_mcgrp(viadev.hca_port_active);
#endif

#endif


#ifdef ADAPTIVE_RDMA_FAST_PATH

    if (viadev_num_rdma_buffer) {
        /* unpin rdma buffers */
        for (i = 0; i < viadev.np; i++) {

            viadev_connection_t *c = &viadev.connections[i];

            if (i == viadev.me) {
                continue;
            }
            if (viadev_use_on_demand) {
                if (MPICM_IB_RC_PT2PT != cm_conn_state[i]) {
                    continue;
                }
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

            /* free the buffers */
            if (c->RDMA_send_buf)
                free(c->RDMA_send_buf);
            if (c->RDMA_recv_buf)
                free(c->RDMA_recv_buf);
            if (c->RDMA_send_buf_DMA)
                free(c->RDMA_send_buf_DMA);
            if (c->RDMA_recv_buf_DMA)
                free(c->RDMA_recv_buf_DMA);

        }
    }
#endif

    if (NULL == viadev.qp_hndl) {
        error_abort_all(GEN_EXIT_ERR, "Null queue pair handle");
    }
    if (viadev_use_on_demand) {
        for (i = 0; i < viadev.np; i++) {
            if (MPICM_IB_RC_PT2PT == cm_conn_state[i]) {
#ifdef XRC
                if (!viadev_use_xrc || 
                        viadev.connections[i].xrc_flags & 
                        XRC_FLAG_DIRECT_CONN)
#endif
                if (ibv_destroy_qp(viadev.qp_hndl[i])) {
                    error_abort_all(IBV_RETURN_ERR, "could not destroy QP");
                }
            }
        }
        MPICM_Finalize_UD();
        if (viadev.ud_qpn_table)
            free(viadev.ud_qpn_table);
        if (viadev.pending_req_head)
            free(viadev.pending_req_head);
        if (viadev.pending_req_tail)
            free(viadev.pending_req_tail);
    }
    else {
        for (i = 0; i < viadev.np; i++) {
            if (i == viadev.me) {
                continue;
            }
            if (ibv_destroy_qp(viadev.qp_hndl[i])) {
                error_abort_all(IBV_RETURN_ERR, "could not destroy QP");
            }
        }
    }

    free(viadev.qp_hndl);

    /* The SRQ limit event thread might be in the
     * middle of posting, so wait until its done! */
    if(viadev_use_srq) {
        pthread_spin_lock(&viadev.srq_post_spin_lock);
    }

    /* Cancel thread if active */
    if(pthread_cancel(viadev.async_thread)) {
        error_abort_all(GEN_ASSERT_ERR,"Failed to cancel async thread\n");
    }

    pthread_join(viadev.async_thread,NULL);

    if(viadev_use_srq) {
        pthread_spin_unlock(&viadev.srq_post_spin_lock);
    }

    if (viadev_async_progress) {
        viadev_finalize_async_progress();
    }

    if(viadev_use_srq) {
        pthread_cond_destroy(&viadev.srq_post_cond);
        pthread_mutex_destroy(&viadev.srq_post_mutex_lock);

        if (ibv_destroy_srq(viadev.srq_hndl)) {
            error_abort_all(IBV_RETURN_ERR, "Couldn't destroy SRQ\n");
        }
    }

    {
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
    else if (viadev_async_progress) {
        if (ibv_destroy_comp_channel(viadev.comp_channel)) {
            error_abort_all(IBV_RETURN_ERR, "Cannot clear completion handler");
        }
    }

    deallocate_vbufs();

    /* unregister all user buffer registration */
    while (dreg_evict());

#ifdef XRC
    if (viadev_use_xrc) {
        char xrc_file[256+26+22+3];

        free (viadev.hostids);
        if (viadev_multihca)
            free (viadev.def_hcas);

        clear_xrc_hash ();

        if (ibv_close_xrc_domain (viadev.xrc_info->xrc_domain))
            error_abort_all (IBV_RETURN_ERR, "Error closing XRC domain");

        close (viadev.xrc_info->fd);

        sprintf (xrc_file, "/dev/shm/mvapich-xrc-%d-%s-%d-%d.tmp", 
                viadev.global_id, viadev.my_name, getuid (), 
                viadev_default_hca);
        unlink (xrc_file);
    }
#endif

    if (ibv_dealloc_pd(viadev.ptag)) {
        error_abort_all(IBV_RETURN_ERR, "could not dealloc PD");
    }

    /* to release all resources */
    if (ibv_close_device(viadev.context)) {
        error_abort_all(IBV_RETURN_ERR, "could not close device");
    }
}


/* gethostname does return a NULL-terminated string if
 * the hostname length is less than "HOSTNAME_LEN". Otherwise,
 * it is unspecified.
 */
static int get_host_id(char *myhostname, int hostname_len)
{
    int host_id = 0;
    struct hostent *hostent;

    hostent = gethostbyname(myhostname);
    if (hostent == NULL) {
        error_abort_all(GEN_EXIT_ERR,
                        "Null value returned by gethostbyname");
    }

    host_id = ((struct in_addr *) hostent->h_addr_list[0])->s_addr;

    return host_id;
}


#ifdef MCST_SUPPORT

 /* Creation of a separate UD_QP and attaching the QP to the mcst_grp */
static void ib_ud_qp_init(void)
{
    struct ibv_qp_init_attr qp_init_attr;
    struct ibv_qp_attr qp_attr;
    int ret;

    viadev.ud_scq_hndl =
        ibv_create_cq(viadev.context, DEFAULT_MAX_CQ_SIZE, NULL, NULL, 0);

    if (!viadev.ud_scq_hndl) {
        error_abort_all(IBV_RETURN_ERR, "Cannot allocate CQ for UD-QP\n");
    }

    viadev.ud_rcq_hndl =
        ibv_create_cq(viadev.context, DEFAULT_MAX_CQ_SIZE, NULL, NULL, 0);

    if (!viadev.ud_rcq_hndl) {
        error_abort_all(IBV_RETURN_ERR, "Cannot allocate CQ for UD-QP");
    }

    memset(&qp_init_attr, 0, sizeof(qp_init_attr));

    qp_init_attr.cap.max_recv_wr = DEFAULT_MAX_WQE;
    qp_init_attr.cap.max_send_wr = DEFAULT_MAX_WQE;

    qp_init_attr.cap.max_recv_sge = viadev_default_max_sg_list;
    qp_init_attr.cap.max_send_sge = viadev_default_max_sg_list;
    qp_init_attr.cap.max_inline_data = 0;

    qp_init_attr.recv_cq = viadev.ud_rcq_hndl;
    qp_init_attr.send_cq = viadev.ud_scq_hndl;
    qp_init_attr.sq_sig_all = 0;
    qp_init_attr.qp_type = IBV_QPT_UD;

    viadev.ud_qp_hndl = ibv_create_qp(viadev.ptag, &qp_init_attr);

    if (!viadev.ud_qp_hndl) {
        error_abort_all(IBV_RETURN_ERR, "Cannot create UD-QP");
    }

    memset(&qp_attr, 0, sizeof(qp_attr));

    qp_attr.qp_state = IBV_QPS_INIT;
     set_pkey_index(&attr.pkey_index,viadev_default_port);
    qp_attr.port_num = viadev_default_port;
    qp_attr.qkey = 0;
    if (ret = ibv_modify_qp(viadev.ud_qp_hndl, &qp_attr,
                            IBV_QP_STATE |
                            IBV_QP_PKEY_INDEX |
                            IBV_QP_PORT | IBV_QP_QKEY)) {
        printf("ret value:%d\n", ret);
        error_abort_all(IBV_RETURN_ERR, "Could not modify UD-QP to INIT");
    }

    D_PRINT("Modified UD to init..Qp\n");

    /**********************  INIT --> RTR  ************************/
    memset(&qp_attr, 0, sizeof(qp_attr));

    qp_attr.qp_state = IBV_QPS_RTR;

    if (ibv_modify_qp(viadev.ud_qp_hndl, &qp_attr, IBV_QP_STATE))
        error_abort_all(IBV_RETURN_ERR, "Could not modify UD-QP to RTR");

    D_PRINT("Modified UD to RTR..Qp\n");


    /********************* RTR --> RTS  * *******************/
    qp_attr.qp_state = IBV_QPS_RTS;
    qp_attr.sq_psn = viadev_default_psn;

    if (ibv_modify_qp(viadev.ud_qp_hndl, &qp_attr,
                      IBV_QP_STATE | IBV_QP_SQ_PSN))
        error_abort_all(IBV_RETURN_ERR, "Could not modify UD-QP to RTS");

    D_PRINT("Modified UD to RTS..Qp\n");

    /* Attach to the mulitcast grp - currently all nodes are in bcast grp */

    if (ibv_attach_mcast(viadev.ud_qp_hndl, &viadev.mcg_dgid, 0))
        error_abort_all(IBV_RETURN_ERR,
                        "Could not attach UD-QP to MCGrp\n");
}

static void ud_mcst_av_init(void)
{
    memset(&viadev.av, 0, sizeof(viadev.av));
    viadev.av.grh.dgid.raw[0] = 0;
    viadev.av.dlid = mlid;

    viadev.av.is_global = TRUE;
    viadev.av.grh.flow_label = DEFAULT_FLOW_LABEL;
    viadev.av.grh.hop_limit = DEFAULT_HOP_LIMIT;
    viadev.av.grh.sgid_index = 0;
    viadev.av.grh.traffic_class = DEFAULT_TRAFFIC_CLASS;
    memcpy(viadev.av.grh.dgid.raw, viadev.mcg_dgid.raw,
           sizeof(union ibv_gid));
    viadev.av.sl = viadev_default_service_level;
    viadev.av.port_num = viadev.hca_port_active;
    viadev.av.src_path_bits = viadev_default_src_path_bits;
    viadev.av.static_rate = viadev_default_static_rate;

    viadev.av_hndl = ibv_create_ah(viadev.ptag, &viadev.av);

    if (!viadev.av_hndl) {
        error_abort_all(IBV_RETURN_ERR,
                        "Could not create address vector\n");
    }
}


static void alloc_ack_region()
{

    int mem_total;
    int alignment = 4;          /* aligning to a 4 byte boundary */

    if (LEFT_OVERS == 0) {
        mem_total =
            (NUM_CHILD * NUM_COROOTS + NUM_COROOTS +
             NUM_WBUFFERS) * ACK_LEN + alignment - 1;
    } else {
        if (viadev.me == 0) {
            mem_total =
                ((NUM_CHILD + LEFT_OVERS) * NUM_COROOTS + NUM_COROOTS +
                 NUM_WBUFFERS) * ACK_LEN + alignment - 1;
        } else {
            if (viadev.me >= NUM_CHILD * NUM_COROOTS) {
                mem_total = (viadev.np + 1 + NUM_WBUFFERS) * ACK_LEN
                    + alignment - 1;
            } else {
                mem_total =
                    (NUM_CHILD * NUM_COROOTS + NUM_COROOTS +
                     NUM_WBUFFERS) * ACK_LEN + alignment - 1;
            }
        }

    }

    viadev.bcast_info.ack_region = malloc(mem_total);
    memset(viadev.bcast_info.ack_region, 0, mem_total);

    viadev.bcast_info.ack_buffer =
        (void
         *) (((aint_t) viadev.bcast_info.ack_region +
              (aint_t) (alignment - 1)) & ~((aint_t) alignment - 1));

    viadev.bcast_info.ack_mem_hndl =
        ibv_reg_mr(viadev.ptag, viadev.bcast_info.ack_buffer,
                   mem_total - alignment - 1,
                   IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);

    co_print("local mem:%p\n", viadev.bcast_info.ack_buffer);
    if (!(viadev.bcast_info.ack_mem_hndl)) {
        error_abort_all(IBV_RETURN_ERR, "Couldn't register memory\n");
    }
    co_print("local r_key:%d\n", mr_out.r_key);
}

static void alloc_bcast_buffers(void)
{
    int i, j;
    int *ack_ptr = (int *) bcast_info->ack_buffer;
    int num_coroots, num_child;

    if (LEFT_OVERS == 0) {
        num_coroots = NUM_COROOTS;
        num_child = NUM_CHILD;
    } else {
        if (viadev.me > LAST_NODE) {
            num_coroots = 1;
            num_child = viadev.np;
        } else {
            if (viadev.me == 0) {
                num_coroots = NUM_COROOTS;
                num_child = NUM_CHILD + LEFT_OVERS;
            } else {
                num_coroots = NUM_COROOTS;
                num_child = NUM_CHILD;
            }
        }
    }

    bcast_info->ack_cnt = malloc((sizeof(int)) * (viadev.np));
    bcast_info->msg_cnt = malloc((sizeof(int)) * (viadev.np));
    bcast_info->time_out =
        (double **) malloc((sizeof(double *)) * num_coroots);
    bcast_info->win_head = malloc((sizeof(int)) * num_coroots);
    bcast_info->win_tail = malloc((sizeof(int)) * num_coroots);
    bcast_info->buf_head = malloc((sizeof(int)) * num_coroots);
    bcast_info->buf_tail = malloc((sizeof(int)) * num_coroots);
    bcast_info->min_ack = malloc((sizeof(int)) * num_coroots);
    bcast_info->is_full = malloc((sizeof(int)) * num_coroots);
    bcast_info->co_buf = malloc((sizeof(vbuf *)) * num_coroots);


    for (i = 0; i < num_coroots; i++) {
        bcast_info->win_head[i] = bcast_info->win_tail[i] = 0;
        bcast_info->buf_head[i] = bcast_info->buf_tail[i] = 0;
        bcast_info->sbuf_head = bcast_info->sbuf_tail = 0;
        bcast_info->ack_full = 0;
        bcast_info->is_full[i] = 0;
        bcast_info->co_buf[i] = malloc((sizeof(vbuf)) * SENDER_WINDOW);
        bcast_info->time_out[i] =
            (double *) malloc((sizeof(double)) * SENDER_WINDOW);
        bcast_info->min_ack[i] = -1;

    }

    for (i = 0; i < viadev.np; i++)
        bcast_info->Bcnt[i] = 0;

    for (i = 0; i < num_child * num_coroots + num_coroots + NUM_WBUFFERS;
         i++)
        ack_ptr[i] = -1;

    for (i = 0; i < num_coroots; i++) {
        for (j = 0; j < SENDER_WINDOW; j++)
            bcast_info->time_out[i][j] = -1;
    }

    bcast_info->bcast_called = 0;
}

void ud_cleanup(void)
{

    int i;
    int num_coroots;

    if (LEFT_OVERS == 0) {
        num_coroots = NUM_COROOTS;
    } else {
        if (viadev.me > LAST_NODE) {
            num_coroots = 1;
        } else {
            num_coroots = NUM_COROOTS;
        }
    }


    free(bcast_info->ack_cnt);
    free(bcast_info->msg_cnt);
    free(bcast_info->win_head);

    free(bcast_info->win_tail);
    free(bcast_info->buf_head);
    free(bcast_info->buf_tail);
    free(bcast_info->is_full);
    free(bcast_info->buf_credits);
    free(bcast_info->buf_updates);

    for (i = 0; i < num_coroots; i++) {
        free(bcast_info->co_buf[i]);
        free(bcast_info->time_out[i]);
    }
    free(bcast_info->time_out);
    free(bcast_info->co_buf);
    ibv_detach_mcast(viadev.ud_qp_hndl, &viadev.mcg_dgid, 0);
    ibv_destroy_qp(viadev.ud_qp_hndl);
    ibv_destroy_ah(viadev.av_hndl);
    ibv_destroy_cq(viadev.ud_scq_hndl);
    ibv_destroy_cq(viadev.ud_rcq_hndl);
    ibv_dereg_mr(bcast_info->ack_mem_hndl);
    free(bcast_info->ack_buffer);
}

#endif

#ifdef PENTIUM_MHZ
double get_us(void)
{
    unsigned long long now;
    __asm __volatile(".byte 0x0f, 0x31":"=A"(now));
    return (double) ((double) now / (PENTIUM_MHZ));
}
#else
double get_us(void)
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return (double) t.tv_sec * (double) 1e6 + (double) t.tv_usec;
}
#endif

/* vi:set sts=4 sw=4 tw=80: */
