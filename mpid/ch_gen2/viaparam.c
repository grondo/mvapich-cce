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


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <infiniband/umad.h>
#include "collutils.h"
#include "viaparam.h"
#include "vbuf.h"
#include "viapacket.h"
#include "ibverbs_const.h"
#include "mpid_smpi.h"
#include "nfr.h"
#include "ib_init.h"

/*
 * ==============================================================
 * Initialize global parameter variables to default values
 * ==============================================================
 */

/* 
 *  Which HCA device is used?
 *  (1) If a user specifies "VIADEV_DEVICE" environmental variable,
 *      the specified HCA device will be used.
 *  (2) Otherwise, MVAPICH first gets a list of the device IDs 
 *      of the available HCA devices on the physical node.
 *  (3) Then, it chooses the first device by default.
 */
#define DEFAULT_HCA_ID      (0)

/*  Limit the size of the registration cache. This is the amount of
 *  pages registered but not used.
 *  Note that for overlapping pages the pages can be counted more
 *  than once. To fix that one can change the caching algorithm so that
 *  each page is only registered once.
 *  VIADEV_REG_CACHE_HAVE_LIMIT flag enables the cache limit feature.
 *  VIADEV_DREG_CACHE_LIMIT = 0 indicates no cache limit.
 */

unsigned long int viadev_dreg_cache_limit = 0;

/* Max number of entries on the Send Q of QPs per connection.
 * Should be about (prepost_depth + extra).
 * Must be within NIC MaxQpEntries limit.
 * Size will be adjusted below.
 */
unsigned long viadev_sq_size = 64;

/* How many sWQEs can be in use before we start doing coalescing 
 * for small messages
 */
unsigned long viadev_coalesce_threshold = 4;

/* Messages under this size are eligible to be coalesced.
 * This is set to a very large default since messages that cannot
 * be coalesced anymore are sent in a timely fashion regardless.
 */
unsigned long viadev_coalesce_threshold_size = 1024 * 1024;

/* Max number of entries on the RecvQ of QPs per connection.
 * computed to be:
 * prepost_depth + viadev_prepost_rendezvous_extra + viadev_prepost_noop_extra
 * Must be within NIC MaxQpEntries limit.
 */
unsigned long viadev_rq_size;

/* After we reach a certain number of processes we stop using
 * inline to save memory
 */
int viadev_no_inline_threshold = 256;

/* Max number of entries on the completion queue at a time.
 * Needed when constructing the completion queue, but can
 * be grown dynamically if needed.
 * Should be about (viadev_sq_size + viadev_rq_size)*num_vi.
 * Must be within NIC MaxCQEntries limit.
 * Size will be adjusted below.
 */
unsigned long viadev_cq_size = 40000;

/* number of vbufs used in RDMA_FAST_PATH on each VI */
int viadev_num_rdma_buffer = VIADEV_NUM_RDMA_BUFFER;

/* default protocol to use for RNDV transfers */
int viadev_rndv_protocol = VIADEV_PROTOCOL_RPUT;
int viadev_async_progress = 0;
int viadev_async_schedule_policy = SCHED_OTHER;

/* if the reg cache is turned off, use R3 for messages below this size
 * (bytes) 
 */
int viadev_r3_nocache_threshold = 1024 * 1024;

/* messages under this size should use use R3 when using the RNDV
 * protocol
 */
int viadev_r3_threshold = 2048;

int viadev_on_demand_threshold = 32;
int viadev_use_on_demand = 0;

#ifdef XRC
/* Enable XRC (on-demand) */
int viadev_use_xrc = 0;
#endif 

#ifdef ADAPTIVE_RDMA_FAST_PATH

/* Adaptive RDMA Eager flow
 * Added two configuration parameters:
 *
 * > viadev_rdma_eager_limit
 *       Limits the number of connections that can be upgraded to use
 *       the RDMA buffers for Eager flow.
 *       If this parameter is not set, there will be no limit. 
 *       -1 : The limit is 2 * log2(number of processes)
 *       Any other value will be the actual limit.
 *
 * > viadev_rdma_eager_threshold
 *       Define the number of Eager packets the receiver will get
 *       before it decide to alloacte RDMA buffers for this connection
 *       -1 : allocate buffers on initialization.
 *
 * > viadev_adaptive_enable_limit
 *       Defines the number of processes from which the adaptive flow
 *       will be enabled. If the number of processes is under the limit, 
 *       the rdma fast path will be enabled from 0 packets.
 */
int viadev_rdma_eager_limit = 64;
int viadev_rdma_eager_threshold = 10;
int viadev_adaptive_enable_limit = 32;

#endif

/*
 * R3 path can generate many many packets for one large
 * MPI message. We will limit the number of packets on
 * the wire to prevent dumping lot of messages at the
 * receiver
 */

int      viadev_max_r3_pending_data = (512 * 1024);

uint32_t viadev_srq_alloc_size = 4096;
uint32_t viadev_srq_fill_size = 256;
uint32_t viadev_srq_limit = 30;
uint32_t viadev_srq_zero_post_max = 1;

/*
 * By default, we will use shared receive queues 
 */
int      viadev_use_srq = 1;


/* By default we have eager message coalescing
 * turned on when the compile flag is used
 */

uint32_t viadev_use_eager_coalesce = 1;


/* Should we only coalesce messages with identical header 
 * information? (tag, context id, src_lrank, and size) 
 */
uint32_t viadev_use_eager_coalesce_limited = 0;

/* how many send operations can be queued before 
 * the eager path is no longer taken
 */

int viadev_eager_ok_threshold = 5;

/* How many vbufs can be in the ext_sendq or backlog until
 * we start calling progress in various send operations
 */

int viadev_progress_threshold = 1;

/* number of vbufs to pre-post as receives on each VI */
int viadev_prepost_depth = VIADEV_PREPOST_DEPTH;
int viadev_prepost_threshold = 10;
int viadev_initial_prepost_depth = 12;
int viadev_initial_credits;

/* number of additional initial vbufs to allocate per VI
 * after initial allocation is exhausted.
 */
static int viadev_vbuf_extra = 10;


/* The total size of each vbuf. Used to be the eager threshold, but
 * it can be smaller, so that each eager message will span over few
 * vbufs
 */
int viadev_vbuf_total_size = (12 * 1024);

/* In the case of small numbers of VIs, allocate at least
 * this number of VBUFs whenever a new region is allocated.
 */

/* number of vbufs to allocate initially.
 * This will be re-defined after reading the parameters below
 * to scale to the number of VIs and other factors.
 */
int viadev_vbuf_pool_size = 512;

/* number of vbufs to allocate in a secondary region if we should
 * run out of the initial allocation.  This is re-computed (below)
 * once other parameters are known.
 */
int viadev_vbuf_secondary_pool_size = 128;

/* max (total) number of vbufs to allocate, after which process 
 * terminates with a fatal error.
 * -1 means no limit.
 */
int viadev_vbuf_max = -1;

/* The number of "extra" vbufs that will be posted as receives
 * on a connection in anticipation of an R3 rendezvous message.
 * The TOTAL number of VBUFs posted on a receive queue at any
 * time is viadev_prepost_depth + viadev_prepost_rendezvous_extra
 * regardless of the number of outstanding R3 sends active on
 * a connection.
 */
int viadev_prepost_rendezvous_extra = VIADEV_PREPOST_RENDEZVOUS_EXTRA;

/* The message size (in bytes) at which we switch from an EAGER
 * protocol to the three-way RENDEZVOUS protocol.
 * In reality, smaller messages might be sent using R3
 * rather than EAGER if remote credits are low.
 *
 * This value will be set in viadev_set_default_parameters()
 */
int viadev_rendezvous_threshold = 0;

/* allow some extra buffers for non-credited packets (eg. NOOP) */
#ifdef MEMORY_RELIABLE
int viadev_prepost_noop_extra = 16;
#else
int viadev_prepost_noop_extra = 8;
#endif



/* dont allow EAGER sends or R3 data packets to consume all
 * remote credits on a connection.  This parameter records
 * the number of credits per connection that will be preserved
 * for non-data, control packets.
 *
 * By default, the value best for srq is used, since
 * srq is default. For send/receive, this value is 10
 * by default (set later in viadev_init_parameters()
 */
int viadev_credit_preserve = 100;

/* As receive descriptors (VBUFs) are consumed and reaped on
 * a connection, the receive side posts more receive VBUFs.
 * Each time a message is sent to the remote end of a connection,
 * a credit update value, representing the number of new receive
 * VBUFs posted since the last message was sent, is piggybacked
 * on the message.  The remote end can then update its remote
 * credit value, possibly unblocking stalled R3 data sends.
 * If the receiving side has no reason to send a message, the
 * credit update accumulate and do not get communicated to the
 * remote end.  If the credit update value exceeds this
 * viadev_credit_notify_threshold value, a NoOp message is
 * sent to the remote end with the sole purpose of communicating
 * the credit update and thereby preventing deadlock.
 *
 * There are two places where NoOp operations may be sent.
 * The first is after all the entries on the CQ are processed,
 * indicating that all incoming and outgoing messages have
 * completed.  If there are more than viadev_credit_notify_threshold
 * local credits we send a NoOp.
 */
int viadev_credit_notify_threshold = 5;

/* The second place is while we are processing receive descriptors.
 * If the local credit value gets reasonably large, we are not
 * sending messages to the remote side.  The remote end of this
 * connection may be stalled waiting for us to process all the
 * recieves so lets send a NoOp immediately so that they can
 * continue work.
 * This viadev_dynamic_credit_threshold should be a fraction
 * of the maximum receive prepost depth, but substantially
 * greater than the viadev_credit_notify_threshold.
 * It is re-computed below.
 */
int viadev_dynamic_credit_threshold = 10;

/* Environment variables to control Alltoall
 * algorithm being used. By default the values
 * used are the same as default MPICH
 */


/*
 * The number of times MPID_DeviceCheck will poll for
 * messages before yielding and going to sleep. Should
 * be high for lower latencies, and low for more
 * tendency to yield CPU.
 *
 * Valid only when viadev_use_blocking = 1;
 */
int viadev_use_blocking = 0;
unsigned long long viadev_max_spin_count = VIADEV_MAX_SPIN_COUNT;

#ifdef _SMP_
int disable_shared_mem = 0; 
int smp_num_send_buffer = SMP_NUM_SEND_BUFFER;
int smp_batch_size = SMP_BATCH_SIZE;
#ifdef _AFFINITY_
/* Affinity is enabled by default */
unsigned int viadev_enable_affinity=1;
char *cpu_mapping = NULL;
#endif 
#endif

/*
 * Registration cache allows amortization of the buffer
 * registration costs over several uses of the same buffer
 * for sending messages.
 *
 * By default, it is turned on. Set to 0 to disable.
 */
#ifndef DISABLE_PTMALLOC
int viadev_use_dreg_cache = 1;
#else /* disable dreg cache if no PTMALLOC */
int viadev_use_dreg_cache = 0;
#endif

/* 
 * Initialization parameters.
 */
unsigned int      viadev_ndreg_entries = 1000;
int               viadev_default_mtu = IBV_MTU_1024;
unsigned long     viadev_max_rdma_size = VIADEV_MAX_RDMA_SIZE;
uint8_t           viadev_default_qp_ous_rd_atom = VIADEV_DEFAULT_QP_OUS_RD_ATOM;
uint32_t          viadev_default_psn = VIADEV_DEFAULT_PSN;
uint16_t          viadev_default_pkey = VIADEV_DEFAULT_PKEY;
uint16_t          viadev_default_pkey_ix = VIADEV_DEFAULT_PKEY_IX;
uint8_t           viadev_default_min_rnr_timer = VIADEV_DEFAULT_MIN_RNR_TIMER;
uint8_t           viadev_default_service_level = VIADEV_DEFAULT_SERVICE_LEVEL;
uint8_t           viadev_default_time_out = VIADEV_DEFAULT_TIME_OUT;
uint8_t           viadev_default_static_rate = VIADEV_DEFAULT_STATIC_RATE;
uint8_t           viadev_default_src_path_bits = VIADEV_DEFAULT_SRC_PATH_BITS;
int               viadev_max_fast_eager_size = VIADEV_MAX_FAST_EAGER_SIZE;
uint8_t           viadev_default_retry_count = VIADEV_DEFAULT_RETRY_COUNT;
uint8_t           viadev_default_rnr_retry = VIADEV_DEFAULT_RNR_RETRY;
uint32_t          viadev_default_max_sg_list = VIADEV_DEFAULT_MAX_SG_LIST;
uint8_t           viadev_default_max_rdma_dst_ops = VIADEV_DEFAULT_MAX_RDMA_DST_OPS;

int               viadev_default_port = -1;
#ifdef XRC
unsigned int      viadev_default_hca = -1;
#endif
int               viadev_max_ports = 2;
uint32_t          viadev_max_inline_size = 128;

/* Have the LMC usage disabled by default, some problem
 * with the up/down implementation of opensm */

int               disable_lmc = 1;

uint32_t          viadev_debug_level = DEBUG00; /* No debug prints by default */

unsigned int      viadev_multiport = 0;
unsigned int      viadev_multihca = 0;
unsigned int      num_hcas = 0;

/* By Default disable the use of APM and APM Tester */
unsigned int      viadev_use_apm = 0;
unsigned int      viadev_use_apm_test = 0;
unsigned int      apm_count;
unsigned int      viadev_eth_over_ib = 0;

/* Network Fault Resiliency parameters */
unsigned int      viadev_nfr_ack_threshold = 15;
unsigned int      viadev_use_nfr = 0;

/*
 * Set the environment variables relating to
 * HCA type etc. This will be used to open
 * the HCA and do autodetection before all
 * other user set parameters are applied
 */

void viadev_init_hca_parameters()
{
    char *value;

    /* Get the appropriate IB device */
    strncpy(viadev.device_name, VIADEV_INVALID_DEVICE, 32);

    if ((value = getenv("VIADEV_USE_RDMAOE")) != NULL) {
        viadev_eth_over_ib = !!(int)atoi(value);
    }    

    if ((value = getenv("VIADEV_DEVICE")) != NULL) {
        strncpy(viadev.device_name, value, 32);
    }    

    if ((value = getenv("VIADEV_USE_LMC")) != NULL){
        disable_lmc = !(atoi(value));
    }   

    if ((value = getenv("VIADEV_USE_APM")) != NULL){
        viadev_use_apm = atoi(value);
    }   
    
    if ((value = getenv("VIADEV_USE_APM_TEST")) != NULL){
        viadev_use_apm_test = atoi(value);
    }   
    
    if ((value = getenv("VIADEV_APM_COUNT")) != NULL){
        apm_count = atoi(value);
    } else {
        apm_count = APM_COUNT;
    }
    
    if (viadev_use_nfr && (value = getenv("VIADEV_NFR_ACK_THRESHOLD"))) {
        viadev_nfr_ack_threshold = atoi(value);
    }
    
    if ((value = getenv("VIADEV_DEFAULT_PORT")) != NULL) {
        viadev_default_port = atoi(value);
        if(viadev_default_port < 0) {
            error_abort_all(GEN_ASSERT_ERR,
                    "User supplied port (%d) invalid\n",
                    viadev_default_port);
        }
    }

    if ((value = getenv("VIADEV_MULTIPORT")) != NULL) {
        viadev_multiport = atoi(value);
    }

    /* The new alias will override the old 
     * variable, if defined */
    
    if ((value = getenv("VIADEV_USE_MULTIPORT")) != NULL) {
        viadev_multiport = atoi(value);
    }
    
    if ((value = getenv("VIADEV_MULTIHCA")) != NULL) {
        viadev_multihca = atoi(value);
    }

    /* The new alias will override the old 
     * variable, if defined */
    if ((value = getenv("VIADEV_USE_MULTIHCA")) != NULL) {
        viadev_multihca = atoi(value);
    }
    
    if ((value = getenv("VIADEV_MAX_PORTS")) != NULL) {
        viadev_max_ports = atoi(value);
        if(viadev_max_ports < 0) {
            error_abort_all(GEN_ASSERT_ERR,
                    "User supplied max. ports (%d) invalid\n",
                    viadev_max_ports);
        }
    }
}


void viadev_init_parameters(int num_proc, int me)
{
    char *value;

    /* ===============================================================
     * Examine the environment variables to see in any of these
     * were re-defined on the command line.
     * ===============================================================
     */

    if ((value = getenv("VIADEV_DEBUG_LEVEL")) != NULL) {

        viadev_debug_level = DEBUG00 + atoi(value);

        if (viadev_debug_level < DEBUG00 || 
                viadev_debug_level > DEBUG03) {

            error_abort_all(GEN_ASSERT_ERR,
                   "VIADEV_DEBUG_LEVEL wrong value defined -"
                   " %d\n", viadev_debug_level);
        }
    }
        
    if ((value = getenv("VIADEV_PROGRESS_THRESHOLD")) != NULL) {
        viadev_progress_threshold = atoi(value);
    }


    if ((value = getenv("VIADEV_MAX_INLINE_SIZE")) != NULL) {
        viadev_max_inline_size = atoi(value);
        if(viadev_max_inline_size < 0) {
            error_abort_all(GEN_ASSERT_ERR,
                    "User supplied max. inline size (%d) invalid\n",
                    viadev_max_inline_size);
        }
    }


    if ((value = getenv("VIADEV_DREG_CACHE_LIMIT")) != NULL) {
        viadev_dreg_cache_limit = atol(value);
    }


    if ((value = getenv("VIADEV_MAX_RDMA_SIZE")) != NULL) {
        viadev_max_rdma_size = (unsigned long)atol(value);
    }
    if ((value = getenv("VIADEV_DEFAULT_MTU")) != NULL) {
        if (strncmp(value,"MTU256", 6)==0)
            viadev_default_mtu = IBV_MTU_256;
        else if (strncmp(value,"MTU512", 6)==0)
            viadev_default_mtu = IBV_MTU_512;
        else if (strncmp(value,"MTU1024", 6)==0)
            viadev_default_mtu = IBV_MTU_1024;
        else if (strncmp(value,"MTU2048", 6)==0)
            viadev_default_mtu = IBV_MTU_2048;
        else if (strncmp(value,"MTU4096", 6)==0)
            viadev_default_mtu = IBV_MTU_4096;
        else
            viadev_default_mtu = IBV_MTU_1024;
    }

    if ((value = getenv("VIADEV_USE_NFR")) != NULL) {
        viadev_use_nfr = !!atoi(value);
        if(viadev_use_nfr) {
            viadev_rndv_protocol = VIADEV_PROTOCOL_RGET;
        }
    }

    if ((value = getenv("VIADEV_RNDV_PROTOCOL")) != NULL) {
        if (strncmp(value,"RPUT", 4)==0)
            viadev_rndv_protocol = VIADEV_PROTOCOL_RPUT;
        else if (strncmp(value,"RGET", 4)==0)
            viadev_rndv_protocol = VIADEV_PROTOCOL_RGET;
        else if (strncmp(value,"R3", 2)==0)
            viadev_rndv_protocol = VIADEV_PROTOCOL_R3;
        else if (strncmp(value,"ASYNC", 5)==0) {
            viadev_rndv_protocol = VIADEV_PROTOCOL_RGET;
            viadev_async_progress = 1;
        }
        else
            error_abort_all(GEN_EXIT_ERR, "VIADEV_RNDV_PROTOCOL "
                    "must be either \"RPUT\", \"RGET\", \"ASYNC\",  or \"R3\"");

    }

    if ((value = getenv("VIADEV_ASYNC_SCHEDULE")) != NULL) {
        if (strncmp(value,"DEFAULT", 7)==0)
            viadev_async_schedule_policy = SCHED_OTHER;
        else if (strncmp(value,"FIFO", 4)==0)
            viadev_async_schedule_policy = SCHED_FIFO;
        else if (strncmp(value,"RR", 2)==0)
            viadev_async_schedule_policy = SCHED_RR;
        else
            error_abort_all(GEN_EXIT_ERR, "VIADEV_ASYNC_SCHEDULE "
                    "must be either \"DEFAULT\", \"FIFO\" or \"RR\"");
    }


    if ((value = getenv("VIADEV_R3_NOCACHE_THRESHOLD")) != NULL) {
        viadev_r3_nocache_threshold = (int)atoi(value);
    }

    if ((value = getenv("VIADEV_R3_THRESHOLD")) != NULL) {
        viadev_r3_threshold = (int)atoi(value);
    }

    if ((value = getenv("VIADEV_DEFAULT_QP_OUS_RD_ATOM")) != NULL) {
        viadev_default_qp_ous_rd_atom = (uint8_t)atoi(value);
    }

    if ((value = getenv("VIADEV_DEFAULT_PSN")) != NULL) {
        viadev_default_psn = (uint32_t)atol(value);
    }

    if ((value = getenv("VIADEV_DEFAULT_PKEY")) != NULL) {
        viadev_default_pkey = (uint16_t)strtol(value, (char **) NULL,0) & PKEY_MASK;
    }

    if ((value = getenv("VIADEV_DEFAULT_MIN_RNR_TIMER")) != NULL) {
        viadev_default_min_rnr_timer = (uint8_t)atol(value);
    }
    if ((value = getenv("VIADEV_DEFAULT_SERVICE_LEVEL")) != NULL) {
        viadev_default_service_level = (uint8_t)atol(value);
    }
    if ((value = getenv("VIADEV_DEFAULT_TIME_OUT")) != NULL) {
        viadev_default_time_out = (uint8_t)atol(value);
    }
    if ((value = getenv("VIADEV_DEFAULT_STATIC_RATE")) != NULL) {
        viadev_default_static_rate = (uint8_t)atol(value);
    }
    if ((value = getenv("VIADEV_DEFAULT_SRC_PATH_BITS")) != NULL) {
        viadev_default_src_path_bits = (uint8_t)atoi(value);
    }
    if ((value = getenv("VIADEV_MAX_FAST_EAGER_SIZE")) != NULL) {
        viadev_max_fast_eager_size = (int)atol(value);
    }
    if ((value = getenv("VIADEV_DEFAULT_RETRY_COUNT")) != NULL) {
        viadev_default_retry_count = (uint8_t)atol(value);
    }
    if ((value = getenv("VIADEV_DEFAULT_RNR_RETRY")) != NULL) {
        viadev_default_rnr_retry = (uint8_t)atol(value);
    }
    if ((value = getenv("VIADEV_DEFAULT_MAX_SG_LIST")) != NULL) {
        viadev_default_max_sg_list = (uint32_t)atol(value);
    }
    if ((value = getenv("VIADEV_DEFAULT_MAX_RDMA_DST_OPS")) != NULL) {
        viadev_default_max_rdma_dst_ops = (uint8_t)atol(value);
    }
    if ((value = getenv("VIADEV_NDREG_ENTRIES")) != NULL) {
        viadev_ndreg_entries = (unsigned int)atoi(value);
    }
#ifndef DISABLE_PTMALLOC
    /* if PTMALLOC is turned off, we set viadev_use_dreg_cache=0 above
       don't allow them to turn it back on here */
    if ((value = getenv("VIADEV_USE_DREG_CACHE")) != NULL) {

        viadev_use_dreg_cache = (int)atoi(value);

        if( (viadev_use_dreg_cache < 0) || 
                (viadev_use_dreg_cache > 1)) {

            /* Maybe user was confused while setting this variable,
             * disable dreg cache just to be safe */
            viadev_use_dreg_cache = 0;
        }
    }
#endif 
    if ((value = getenv("VIADEV_SQ_SIZE")) != NULL) {
        viadev_sq_size = atol(value);
    }
    if ((value = getenv("VIADEV_CQ_SIZE")) != NULL) {
        viadev_cq_size = atol(value);
    }
    if ((value = getenv("VIADEV_NUM_RDMA_BUFFER")) != NULL) {
        viadev_num_rdma_buffer = atoi(value);
        if (viadev_num_rdma_buffer < 0)
            viadev_num_rdma_buffer = 0;
        if (viadev_num_rdma_buffer == 1)
            viadev_num_rdma_buffer = 2; /* need 1 extra */
    }

#ifdef ADAPTIVE_RDMA_FAST_PATH
    if ((value = getenv("VIADEV_ADAPTIVE_RDMA_THRESHOLD")) != NULL) {
        viadev_rdma_eager_threshold = atoi(value);
    }
    if ((value = getenv("VIADEV_ADAPTIVE_RDMA_LIMIT")) != NULL) {
        viadev_rdma_eager_limit = atoi(value);
        if (viadev_rdma_eager_limit == -1)
            viadev_rdma_eager_limit = log_2(viadev.np);
    }

    if ((value = getenv("VIADEV_ADAPTIVE_ENABLE_LIMIT")) != NULL) {
        viadev_adaptive_enable_limit = atoi(value);
        if (viadev_adaptive_enable_limit < 0) {
            viadev_adaptive_enable_limit = 0;
        }
    }
    
#endif

#ifdef XRC 
    /* For XRC, on-demand and SRQ need to be enabled and the HCA must
     * support XRC */
    if ((value = getenv ("VIADEV_USE_XRC")) != NULL) {
        viadev_use_xrc = atoi(value) > 0;
        if (viadev_use_xrc) {
            viadev_use_on_demand = 1;
            viadev_use_srq = 1;
            viadev_use_eager_coalesce = 0;

            if(viadev_use_nfr) {
                error_abort_all(GEN_EXIT_ERR, "VIADEV_USE_XRC=1 and VIADEV_USE_NR=1  "
                        "cannot be set at the same time\n");
            }
        }
    }
#endif

#ifdef XRC
    if (1 != viadev_use_xrc)
#endif
    {
        if ((value = getenv("VIADEV_USE_SRQ")) != NULL) {
            viadev_use_srq = atoi(value);

            if(viadev_use_srq != 0) {
                viadev_use_srq = 1;
            }

            if(!viadev_use_srq) {
                viadev_credit_preserve = 10;
            }
        }
    }
    if ((value = getenv("VIADEV_SRQ_MAX_SIZE")) != NULL) {
        viadev_srq_alloc_size = (uint32_t) atoi(value);
    }

    if ((value = getenv("VIADEV_SRQ_SIZE")) != NULL) {
        viadev_srq_fill_size = (uint32_t) atoi(value);
    }

    if ((value = getenv("VIADEV_SRQ_LIMIT")) != NULL) {
        viadev_srq_limit = (uint32_t) atoi(value);

        if(viadev_srq_limit > viadev_srq_fill_size) {
            error_abort_all(GEN_ASSERT_ERR,
                    "SRQ limit shouldn't be greater than SRQ size\n");
        }
    }

    if(viadev_use_srq) {
        viadev_credit_preserve = (viadev_srq_fill_size > 200) ?                                                 (viadev_srq_fill_size - 100) : (viadev_srq_fill_size / 2);
    }


    if ((value = getenv("VIADEV_SRQ_ZERO_POST_MAX")) != NULL) {
        viadev_srq_zero_post_max = atol(value);
    }
#ifdef XRC
    if (1 != viadev_use_xrc) 
#endif
    {
        if ((value = getenv("VIADEV_USE_COALESCE")) != NULL) {
            viadev_use_eager_coalesce = (uint32_t) atoi(value);
        }
        if ((value = getenv("VIADEV_USE_COALESCE_SAME")) != NULL) {
            viadev_use_eager_coalesce_limited = (uint32_t) atoi(value);
            viadev_progress_threshold = 2;
        }
        if ((value = getenv("VIADEV_COALESCE_THRESHOLD_SQ")) != NULL) {
            viadev_coalesce_threshold = (uint32_t) atoi(value);
        }
        if ((value = getenv("VIADEV_COALESCE_THRESHOLD_SIZE")) != NULL) {
            viadev_coalesce_threshold_size = (uint32_t) atoi(value);
        }
    }
   
    if ((value = getenv("VIADEV_EAGER_OK_EXT_LIMIT")) != NULL) {
        viadev_eager_ok_threshold = (int) atoi(value);
    }
    
    if ((value = getenv("VIADEV_PREPOST_DEPTH")) != NULL) {
        viadev_prepost_depth = atoi(value);
        if(viadev_prepost_depth <= 0) {
            viadev_prepost_depth = 1;
        }
    }
    if ((value = getenv("VIADEV_PREPOST_THRESHOLD")) != NULL) {
        viadev_prepost_threshold = atoi(value);
        if(viadev_prepost_threshold <= 0) {
            viadev_prepost_threshold = 1;
        }
    }
    if ((value = getenv("VIADEV_VBUF_POOL_SIZE")) != NULL) {
        viadev_vbuf_pool_size = atoi(value);
        if(viadev_vbuf_pool_size <= 0) {
            viadev_vbuf_pool_size = 1;
        }
    }
    if ((value = getenv("VIADEV_VBUF_TOTAL_SIZE")) != NULL) {
        viadev_vbuf_total_size= atoi(value);
        if(viadev_vbuf_total_size <= 0) {
            viadev_vbuf_total_size = 1;
        }
    }

    if ((value = getenv("VIADEV_VBUF_SECONDARY_POOL_SIZE")) != NULL) {
        viadev_vbuf_secondary_pool_size = atoi(value);
        if ( viadev_vbuf_secondary_pool_size <= 0 ) {
            viadev_vbuf_secondary_pool_size = 1;
        }
    }
    if ((value = getenv("VIADEV_VBUF_EXTRA")) != NULL) {
        viadev_vbuf_extra = atoi(value);
        if( viadev_vbuf_extra <= 0) {
            viadev_vbuf_extra = 1;
        }
    }
    if ((value = getenv("VIADEV_VBUF_MAX")) != NULL) {
        viadev_vbuf_max = atoi(value);
    }

    if ((value = getenv("VIADEV_MAX_R3_PENDING_DATA")) != NULL) {
        viadev_max_r3_pending_data = (int) atoi(value);

        if(viadev_max_r3_pending_data <= viadev_vbuf_total_size) {
            error_abort_all(GEN_ASSERT_ERR, "VIADEV_MAX_R3_PENDING_DATA value"
                    " too small; set to higher than %d\n",
                    viadev_vbuf_total_size);
        }
    }

    if ((value = getenv("VIADEV_PREPOST_RENDEZVOUS_EXTRA")) != NULL) {
        viadev_prepost_rendezvous_extra = atoi(value);
        if(viadev_prepost_rendezvous_extra <= 0) {
            viadev_prepost_rendezvous_extra = 1;
        }
    }
    if ((value = getenv("VIADEV_RENDEZVOUS_THRESHOLD")) != NULL) {
        viadev_rendezvous_threshold = atoi(value);
        if(viadev_rendezvous_threshold <= 0) {
            viadev_rendezvous_threshold = 1;
        }
    }

    if ((value = getenv("VIADEV_PREPOST_NOOP_EXTRA")) != NULL) {
        viadev_prepost_noop_extra = atoi(value);
        if(viadev_prepost_noop_extra <= 0) {
            viadev_prepost_noop_extra = 1;
        }
    }
    if ((value = getenv("VIADEV_INITIAL_PREPOST_DEPTH")) != NULL) {
        viadev_initial_prepost_depth = atoi(value);
        if(viadev_initial_prepost_depth <= 0) {
            viadev_initial_prepost_depth = 1;
        }
    }

    if (viadev_initial_prepost_depth >
        viadev_prepost_depth + viadev_prepost_noop_extra) {
        viadev_initial_prepost_depth =
            viadev_prepost_depth + viadev_prepost_noop_extra;
    }

    if (viadev_initial_prepost_depth <= 0) {
        viadev_initial_prepost_depth = 1;
    }

    if (viadev_initial_prepost_depth <= viadev_prepost_noop_extra) {
        viadev_initial_credits = viadev_initial_prepost_depth;
    } else {
        if(!viadev_use_srq) {
            viadev_initial_credits =
                viadev_initial_prepost_depth - viadev_prepost_noop_extra;
        }
    }

    if ((value = getenv("VIADEV_CREDIT_PRESERVE")) != NULL) {
        viadev_credit_preserve = atoi(value);
        if(viadev_credit_preserve <= 0) {
            viadev_credit_preserve = 1;
        }
    }

    if ((value = getenv("VIADEV_CREDIT_NOTIFY_THRESHOLD")) != NULL) {
        viadev_credit_notify_threshold = atoi(value);
        if(viadev_credit_notify_threshold <= 0) {
            viadev_credit_notify_threshold = 1;
        }
    }
    if ((value = getenv("VIADEV_DYNAMIC_CREDIT_THRESHOLD")) != NULL) {
        viadev_dynamic_credit_threshold = atoi(value);
        if(viadev_dynamic_credit_threshold <= 0) {
            viadev_dynamic_credit_threshold = 1;
        }
    }
    if ((viadev_prepost_depth - viadev_prepost_threshold) <=
        MAX(viadev_credit_notify_threshold,
            viadev_dynamic_credit_threshold)) {

        if(viadev_prepost_depth < 1 + MAX(viadev_credit_notify_threshold,
                    viadev_dynamic_credit_threshold)) {
            error_abort_all(GEN_EXIT_ERR,
                "Error: VIADEV_PREPOST_DEPTH should atleast exceed %d"
                " based on your current parameter set\n", 1 + 
                MAX(viadev_credit_notify_threshold, viadev_dynamic_credit_threshold));
        } else {
            viadev_prepost_threshold =
                viadev_prepost_depth - 1 - MAX(viadev_credit_notify_threshold,
                        viadev_dynamic_credit_threshold);
        }
    }
    if (viadev_prepost_threshold < 0)
        viadev_prepost_threshold = 0;

    /* compute Receive Q size */
    viadev_rq_size =
        viadev_prepost_depth + viadev_prepost_rendezvous_extra +
        viadev_prepost_noop_extra;

     /* Decide whether on demand is used*/
#ifdef XRC
     if (1 != viadev_use_xrc)
#endif 
     {
         value = getenv("VIADEV_ON_DEMAND_THRESHOLD");
         if (value != NULL) {
             viadev_on_demand_threshold = atoi(value);
         }
         if (viadev.np > viadev_on_demand_threshold) {
             viadev_use_on_demand = 1;
         }
         else {
             viadev_use_on_demand = 0;
         }
     }


     {
         if ((value = getenv("VIADEV_NO_INLINE_THRESHOLD")) != NULL) {
             viadev_no_inline_threshold = atoi(value);
             if(viadev_no_inline_threshold < 0) {
                 error_abort_all(GEN_ASSERT_ERR,
                         "User supplied no inline threshold (%d) invalid\n",
                         viadev_no_inline_threshold);
             }
         }

         if(!viadev_use_on_demand && viadev.np > viadev_no_inline_threshold) {
             viadev_max_inline_size = 0;
         }
     }

     /* Blocking options are read last, since some
      * features will be disabled if blocking is
      * enabled */
#ifdef XRC
     if (1 != viadev_use_xrc) 
#endif
     {
        if ((value = getenv("VIADEV_USE_BLOCKING")) != NULL) {
            viadev_use_blocking = atoi(value);

            if(viadev_use_blocking < 0) {
                viadev_use_blocking = 0;
            } 
            
            if (viadev_use_blocking > 1) {
                viadev_use_blocking = 1;
            }

            if(viadev_use_blocking) {

                /* The user wants to use blocking,
                 * so we will internally disable
                 * SMP and RDMA (short messages) */

#ifdef _SMP_
                disable_shared_mem = 1;
#endif
#ifdef ADAPTIVE_RDMA_FAST_PATH
                viadev_rdma_eager_limit = 0;
#endif
                /*Disable on-demand if blocking is used*/
                viadev_use_on_demand = 0;
            }
        }
     }

     if ((value = getenv("VIADEV_MAX_SPIN_COUNT")) != NULL) {
         viadev_max_spin_count = atoll(value);
     }

     if(viadev_use_blocking && viadev_use_nfr) {
         error_abort_all(GEN_ASSERT_ERR,
                 "Cannot use VIADEV_USE_BLOCKING and VIADEV_USE_NR"
                 " at the same time, please disable either one\n");
     }


     /* Default parameters in the presence of memory
      * based reliability */

#ifdef MEMORY_RELIABLE
    viadev_use_srq = 0;
    viadev_credit_preserve = 10;
#endif
}

void dump_param_values(FILE * fd, int me)
{
    char *int_fmt;
    char *str_fmt;

    int_fmt = (char *) "%3d: %32s = %d\n";
    str_fmt = (char *) "%3d: %32s = %s\n";

    /*
       char *hex_fmt = "%3d: %32s = %#x\n";
     */

    fprintf(fd, int_fmt, me, "VIADEV_MAX_RDMA_SIZE", (int)viadev_max_rdma_size);
    fprintf(fd, int_fmt, me, "VIADEV_DEFAULT_MTU", (int)viadev_default_mtu);
    fprintf(fd, int_fmt, me, "VIADEV_DEFAULT_QP_OUS_RD_ATOM", (int)viadev_default_qp_ous_rd_atom);
    fprintf(fd, int_fmt, me, "VIADEV_DEFAULT_PSN", (int)viadev_default_psn);
    fprintf(fd, int_fmt, me, "VIADEV_DEFAULT_PKEY", (int)viadev_default_pkey);
    fprintf(fd, int_fmt, me, "VIADEV_DEFAULT_PKEY_IX", (int)viadev_default_pkey_ix);
    fprintf(fd, int_fmt, me, "VIADEV_DEFAULT_MIN_RNR_TIMER", (int)viadev_default_min_rnr_timer);
    fprintf(fd, int_fmt, me, "VIADEV_DEFAULT_SERVICE_LEVEL", (int)viadev_default_service_level);
    fprintf(fd, int_fmt, me, "VIADEV_DEFAULT_TIME_OUT", (int)viadev_default_time_out);
    fprintf(fd, int_fmt, me, "VIADEV_DEFAULT_STATIC_RATE", (int)viadev_default_static_rate);
    fprintf(fd, int_fmt, me, "VIADEV_DEFAULT_SRC_PATH_BITS", (int)viadev_default_src_path_bits);
    fprintf(fd, int_fmt, me, "VIADEV_MAX_FAST_EAGER_SIZE", (int)viadev_max_fast_eager_size);
    fprintf(fd, int_fmt, me, "VIADEV_DEFAULT_RETRY_COUNT", (int)viadev_default_retry_count);
    fprintf(fd, int_fmt, me, "VIADEV_DEFAULT_RNR_RETRY", (int)viadev_default_rnr_retry);
    fprintf(fd, int_fmt, me, "VIADEV_DEFAULT_MAX_SG_LIST", (int)viadev_default_max_sg_list);
    fprintf(fd, int_fmt, me, "VIADEV_DEFAULT_MAX_RDMA_DST_OPS", (int)viadev_default_max_rdma_dst_ops);
    fprintf(fd, int_fmt, me, "VIADEV_NDREG_ENTRIES", viadev_ndreg_entries);
    fprintf(fd, int_fmt, me, "VIADEV_SQ_SIZE", (int) viadev_sq_size);
    fprintf(fd, int_fmt, me, "VIADEV_CQ_SIZE", (int) viadev_cq_size);
    fprintf(fd, int_fmt, me, "VIADEV_NUM_RDMA_BUFFER",
            viadev_num_rdma_buffer);
    fprintf(fd, int_fmt, me, "VIADEV_PREPOST_DEPTH", viadev_prepost_depth);
    fprintf(fd, int_fmt, me, "VIADEV_INITIAL_PREPOST_DEPTH",
            viadev_initial_prepost_depth);
    fprintf(fd, int_fmt, me, "VIADEV_INITIAL_PREPOST_DEPTH",
            viadev_initial_prepost_depth);
    fprintf(fd, int_fmt, me,
            "VIADEV_PREPOST_THRESHOLD", viadev_prepost_threshold);
    fprintf(fd, int_fmt, me,
            "VIADEV_VBUF_POOL_SIZE", viadev_vbuf_pool_size);
    fprintf(fd, int_fmt, me,
            "VIADEV_VBUF_TOTAL_SIZE", viadev_vbuf_total_size);
    fprintf(fd, int_fmt, me,
            "VIADEV_VBUF_SECONDARY_POOL_SIZE",
            viadev_vbuf_secondary_pool_size);
    fprintf(fd, int_fmt, me, "VIADEV_VBUF_MAX", viadev_vbuf_max);
    fprintf(fd, int_fmt, me,
            "VIADEV_PREPOST_RENDEZVOUS_EXTRA",
            viadev_prepost_rendezvous_extra);
    fprintf(fd, int_fmt, me,
            "VIADEV_PREPOST_NOOP_EXTRA", viadev_prepost_noop_extra);
    fprintf(fd, int_fmt, me, "VIADEV_CREDIT_PRESERVE",
            viadev_credit_preserve);
    fprintf(fd, int_fmt, me, "VIADEV_CREDIT_NOTIFY_THRESHOLD",
            viadev_credit_notify_threshold);
    fprintf(fd, int_fmt, me, "VIADEV_DYNAMIC_CREDIT_THRESHOLD",
            viadev_dynamic_credit_threshold);
    fprintf(fd, int_fmt, me, "VIADEV_RENDEZVOUS_THRESHOLD",
            viadev_rendezvous_threshold);
#ifdef XRC
    fprintf(fd, int_fmt, me, "VIADEV_USE_XRC", viadev_use_xrc);
#endif
}
    
static int get_rate(umad_ca_t *umad_ca)
{
    int i;

    for (i = 1; i <= umad_ca->numports; i++) {
        if (IBV_PORT_ACTIVE == umad_ca->ports[i]->state) {
            return umad_ca->ports[i]->rate;
        }
    }
    return 0;
}

int get_hca_type(struct ibv_device *dev,
        struct ibv_context *ctx)
{
    struct ibv_device_attr dev_attr;
    int hca_type = HCA_ERROR;
    int ret, rate;
    char *dev_name;
    umad_ca_t umad_ca;

    memset(&dev_attr, 0, sizeof(struct ibv_device_attr));

    if(!ctx) {
	return HCA_ERROR;
    }

    ret = ibv_query_device(ctx, &dev_attr);

    if(ret) {
        return HCA_ERROR;
    }

    dev_name = (char *) ibv_get_device_name(dev);

    if (NULL == dev_name) {
        return HCA_ERROR;
    }

    if ( (!strncmp(dev_name, "mthca", 5)) || (!strncmp(dev_name, "mlx4", 4))) {

        if (umad_init() < 0) {
            fprintf(stderr,"Error initializing UMAD library,"
                    " best guess as Mellanox PCI-Ex SDR\n");
            return MLX_PCI_EX_SDR_LION_GEN;
        }

        memset(&umad_ca, 0, sizeof(umad_ca_t));
        ret = umad_get_ca(dev_name, &umad_ca);

        if (ret) {
            fprintf(stderr,"Error getting CA information"
                    " from UMAD library ... taking the"
                    " best guess as Mellanox PCI-Ex SDR\n");
            return MLX_PCI_EX_SDR_LION_GEN;
        }

        if (!strncmp(dev_name, "mthca", 5)) {

            hca_type = MLX_PCI_X_GEN;

            if (!strncmp(umad_ca.ca_type, "MT25", 4)) {

                rate = get_rate(&umad_ca);

                if (!rate) {
                    umad_release_ca(&umad_ca);
                    umad_done();
                    error_abort_all(IBV_RETURN_ERR,
                            "No actives ports was found, aborting\n");
                }

                switch (rate) {
                    case 20:
                        hca_type = MLX_PCI_EX_DDR_LION_GEN;
                        break;
                    case 10:
                        hca_type = MLX_PCI_EX_SDR_LION_GEN;
                        break;
                    default:
                        fprintf(stderr, "Unknown Mellanox PCI-Express HCA"
                                " best guess as Mellanox PCI-Express SDR\n");
                        hca_type = MLX_PCI_EX_SDR_LION_GEN;
                        break;
                }

            } else if (!strncmp(umad_ca.ca_type, "MT23", 4)) {

                hca_type = MLX_PCI_X_GEN;

            } else {

                fprintf(stderr,"Unknown Mellanox HCA type (%s), best"
                        " guess as Mellanox PCI-Express SDR\n",
                        umad_ca.ca_type);
                hca_type = MLX_PCI_EX_SDR_LION_GEN;
            }


        } else { /* This condition should be true : !strncmp(dev_name, "mlx4", 4)*/

            hca_type = CONNECTX_SDR;

            rate = get_rate(&umad_ca);

            switch (rate) {
                case 40:
                    hca_type = CONNECTX_QDR;
                    break;
                case 20:
                    hca_type = CONNECTX_DDR;
                    break;
                case 10:
                    hca_type = CONNECTX_SDR;
                    break;
                default:
                    fprintf(stderr,"Unknown Mellanox PCI-Express HCA, best guess as Mellanox PCI-Express CONNECTX SDR\n");
                    hca_type = CONNECTX_SDR;
                    break;
            }
        } 

        umad_release_ca(&umad_ca);
        umad_done();

    } else if (!strncmp(dev_name, "ipath", 5)) {
        hca_type = PATH_HT;
    } else if (!strncmp(dev_name, "ehca", 4)) {
        hca_type = IBM_EHCA;
    } else {
        hca_type = UNKNOWN_HCA;
    }
    return hca_type;
}

static void viadev_set_compat_params()
{
    viadev_default_mtu = IBV_MTU_1024;
    viadev_use_srq = 1;
    viadev_credit_preserve = 100;
    viadev_initial_credits = viadev_credit_preserve + 100;
    viadev_vbuf_total_size = (9 * 1024);
}

static void viadev_set_common_params()
{
    viadev_rendezvous_threshold = viadev_vbuf_total_size - 
        sizeof(viadev_packet_eager_start);
}

void viadev_set_default_parameters(int nprocs, int me, int do_autodetect, int hca_type)
{
    char *value;
    int tmp;

    if ((value = getenv("VIADEV_USE_COMPAT_MODE")) != NULL) {
        tmp = atoi(value);

        if(tmp) { 
            /* User chose to use COMPAT mode */
            do_autodetect = 0;
        } else {
            /* We will autodetect the configuration,
             * including the non homogeneous case */
            do_autodetect = 1;
        }
    }

    if(do_autodetect) {
        switch(hca_type) {
            case MLX_PCI_X_GEN:
                viadev_default_mtu = IBV_MTU_1024;
                viadev_use_srq = 0;
                viadev_credit_preserve = 10;
                viadev_vbuf_total_size = (12 * 1024);
                break;
            case MLX_PCI_EX_SDR_LION_GEN:
                viadev_default_mtu = IBV_MTU_1024;
                viadev_use_srq = 1;
                viadev_credit_preserve = 100;
                viadev_initial_credits = viadev_credit_preserve + 100;
                viadev_vbuf_total_size = (9 * 1024);
                break;
            case MLX_PCI_EX_DDR_LION_GEN:
                viadev_default_mtu = IBV_MTU_2048;
                viadev_use_srq = 1;
                viadev_credit_preserve = 100;
                viadev_initial_credits = viadev_credit_preserve + 100;
#ifdef _X86_64_
                viadev_vbuf_total_size = (9 * 1024);
#else
                viadev_vbuf_total_size = (6 * 1024);
#endif
                break;
            case CONNECTX_SDR:
                viadev_default_mtu = IBV_MTU_1024;
                viadev_use_srq = 1;
                viadev_credit_preserve = 100;
                viadev_initial_credits = viadev_credit_preserve + 100;
#ifdef _X86_64_
                viadev_vbuf_total_size = (9 * 1024);
#else
                viadev_vbuf_total_size = (6 * 1024);
#endif

                viadev_use_eager_coalesce = 0;
                break;
            case CONNECTX_DDR:
                viadev_default_mtu = IBV_MTU_1024;
                viadev_use_srq = 1;
                viadev_credit_preserve = 100;
                viadev_initial_credits = viadev_credit_preserve + 100;
                viadev_vbuf_total_size = (9 * 1024);
                viadev_use_eager_coalesce = 0;
                break;
            case CONNECTX_QDR:
                viadev_default_mtu = IBV_MTU_2048;
                viadev_use_srq = 1;
                viadev_credit_preserve = 100;
                viadev_initial_credits = viadev_credit_preserve + 100;
                viadev_vbuf_total_size = (9 * 1024);
                viadev_use_eager_coalesce = 0;
                break;
            case PATH_HT:
                viadev_default_mtu = IBV_MTU_1024;
                viadev_use_srq = 1;
                viadev_credit_preserve = 100;
                viadev_initial_credits = viadev_credit_preserve + 100;
                viadev_vbuf_total_size = (9 * 1024);
                viadev_default_qp_ous_rd_atom = 1;
                break;
            case IBM_EHCA:
                viadev_default_mtu = IBV_MTU_1024;
                viadev_use_srq = 0;
                viadev_credit_preserve = 10;
                viadev_vbuf_total_size = (12 * 1024);
                break;
            case RDMAOE:
                viadev_default_mtu = IBV_MTU_1024;
                viadev_use_srq = 1;
                viadev_credit_preserve = 100;
                viadev_initial_credits = viadev_credit_preserve + 100;
#ifdef _X86_64_
                viadev_vbuf_total_size = (9 * 1024);
#else
                viadev_vbuf_total_size = (6 * 1024);
#endif

                viadev_use_eager_coalesce = 1;
                break;
            case HCA_ERROR:
                error_abort_all(IBV_RETURN_ERR,
                        "Could not autodetect HCA, try VIADEV_COMPAT_MODE=1\n");
                break;
            default:
                viadev_set_compat_params();
        }

    } else {

        /* Set compatibility mode;
         * Mellanox PCI-Ex SDR parameters */
        viadev_set_compat_params();
    }

    /* Common defaults */
    viadev_set_common_params();
}
