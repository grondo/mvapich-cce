
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
#include "mv_param.h"
#include "mv_buf.h"
#include "mv_priv.h"
#include "mv_packet.h"
#include "ibverbs_const.h"
#include "mpid_smpi.h"

mvdev_param_info mvparams;

#define DEFAULT_HCA_ID      (0)

#ifdef _SMP_
    int disable_shared_mem = 0;
    int smp_num_send_buffer = SMP_NUM_SEND_BUFFER;
    int smp_batch_size = SMP_BATCH_SIZE;
#ifdef _AFFINITY_
    unsigned int viadev_enable_affinity = 0;
    char *cpu_mapping = NULL;
#endif
#endif

    int viadev_num_rdma_buffer = 32;

void MV_Init_Params(int num_proc, int me) 
{
    char *value;

    D_PRINT("Entering MV_Init_Params\n");

    mvparams.xrc_enabled = 0;

    /*RC */
    mvparams.conn_type = MVDEV_CH_RC_SRQ;

    mvparams.rc_mtu = IBV_MTU_2048;
    mvparams.pkey_ix = 0;
    mvparams.min_rnr_timer = 7;
    mvparams.time_out = 20;
    mvparams.static_rate = 0;
    mvparams.retry_count = 7;
    mvparams.rnr_retry = 7;
    mvparams.max_rdma_dst_ops = 8;
    mvparams.qp_ous_rd_atom = 4;
    mvparams.service_level = 0;

    mvparams.use_header_caching = 1;

    mvparams.rc_setup_threshold = 0;
    mvparams.rc_send_threshold = 2000;

    mvparams.rc_buf_size = 1024 * 8;
    mvparams.rcfp_buf_size = 2048;

    mvparams.rendezvous_threshold = 16384 / 2;

    mvparams.ud_rel = 1;
    mvparams.rc_rel = 0;

    mvparams.use_lmc = 1;
    mvparams.max_lmc_total = 1;
    mvparams.max_sl = 1;

    mvparams.rc_threshold = 0;
    mvparams.max_rc_connections = 16;
    mvparams.max_rcfp_connections = 8;

    mvparams.use_rc_srq = 1;

    mvparams.rc_credits = 80;

    /* RCFP */
    mvparams.rcfp_threshold = 32;

    /* ZCOPY TRANSFER ----------------------------------------- */

    /* Use the zero-copy UD transfer method */
    mvparams.use_zcopy = 1;

    /* Message sizes >= threshold go over zcopy */
    mvparams.zcopy_threshold = 32768;

    /* QPs to be in rndv pool */
    mvparams.rndv_qps = 64;

    /* RELIABILITY TUNING ------------------------------------- */

    /* Time (usec) until ACK status is checked (and ACKs are sent) */
    mvparams.progress_timeout = 1900000;

    /* Time (usec) until a message is resent */
    mvparams.retry_usec = 20000000;

    /* Maximum number of segments outstanding (waiting for ACK) 
     * to any given host 
     */
    mvparams.send_window_segments_max = 400;

    /* Maximum number of out-of-order messages that will be buffered */
    mvparams.recv_window_size_max = 2501;


    /* Maximum number of messages received until an ACK is automatically
     * generated (not waiting for mvparams.progress_timeout */
    mvparams.send_ackafter = 50;
    mvparams.send_ackafter_progress = 10;

    /* maximum number of times to retry sending the same message */
    mvparams.max_retry_count = 50;

    /* RECEIVE BUFFERS ---------------------------------------- */

    /* Total number of recieve buffers to be posted */
    mvparams.recvq_size = 8192;

    /* Re-post buffers when the number of buffers
     * posted decreases below this threshold 
     *   - mvparams.credit_preserve: hard limit
     *   - mvparams.credit_preserve_lazy: only if no messages
     *        have been coming in recently
     */
    mvparams.credit_preserve = 8100;
    mvparams.credit_preserve_lazy = 8146;

    /* QP PARAMETERS ------------------------------------------ */

    mvparams.cq_size = 40000;
    mvparams.ud_sq_size = 2000;
    mvparams.ud_rq_size = 5000;

    mvparams.rc_sq_size = 128;
    mvparams.rc_rq_size = 64;

    mvparams.psn = 0;

    mvparams.ud_max_inline = -1;
    mvparams.rc_max_inline = 128;

    /* MEMORY REGISTRATION ------------------------------------ */

    /* Use the registration cache */
#ifndef DISABLE_PTMALLOC
    mvparams.use_dreg_cache = 1;
#else /* disable dreg cache if no PTMALLOC */
    mvparams.use_dreg_cache = 0;
#endif

    /* number of entries */
    mvparams.ndreg_entries = 1000;
    mvparams.dreg_cache_limit = 0;

    /* OTHER PARAMETERS --------------------------------------- */

    mvparams.r3_segments_max = 300;

    mvparams.default_port = 1;

    /* How many QPs are used to send messages on (round-robin) */
    mvparams.num_qps = 1;

    /* Maximum message transfer size for UD.
     *   -1: maximum that is active
     *    *: otherwise use the value specified
     */
    mvparams.mtu = -1;

    mvparams.maxtransfersize = 1024 * 1024 * 20;

    mvparams.use_profile = 0;

    mvparams.msrq = 1;
    mvparams.xrcshared = 1;
    mvparams.srq_size = 512;

    /* Get the appropriate IB device */
    strncpy(mvparams.device_name, MVDEV_INVALID_DEVICE, 32);

    if ((value = getenv("MV_CONN_TYPE")) != NULL) {
        if (strncmp(value,"RC_SRQ", 6)==0)
            mvparams.conn_type = MVDEV_CH_RC_SRQ;
        else if (strncmp(value,"RC_RQ", 5)==0)
            mvparams.conn_type = MVDEV_CH_RC_RQ;
        else if (strncmp(value,"RC_MULT_SRQ", 11)==0)
            mvparams.conn_type = MVDEV_CH_RC_MULT_SRQ;
        else if (strncmp(value,"XRC_SINGLE_MULT_SRQ", 19)==0) {
            mvparams.conn_type = MVDEV_CH_XRC_SHARED_MULT_SRQ;
            mvparams.xrcshared = 0;
            mvparams.xrc_enabled = 1;
        } else if (strncmp(value,"XRC_SHARED_MULT_SRQ", 19)==0) {
            mvparams.conn_type = MVDEV_CH_XRC_SHARED_MULT_SRQ;
            mvparams.xrc_enabled = 1;
        } else if (strncmp(value,"XRC_SHARED_SRQ", 14)==0) {
            mvparams.conn_type = MVDEV_CH_XRC_SHARED_MULT_SRQ;
            mvparams.msrq = 0;
            mvparams.xrc_enabled = 1;
        } else if (strncmp(value,"XRC_SINGLE_SRQ", 14)==0) {
            mvparams.conn_type = MVDEV_CH_XRC_SHARED_MULT_SRQ;
            mvparams.msrq = 0;
            mvparams.xrcshared = 0;
            mvparams.xrc_enabled = 1;
        } else {
            mvparams.conn_type = MVDEV_CH_RC_RQ;
            fprintf(stderr, "ERROR: Invalid conn type\n");
        }
    }

    if((value = getenv("MV_RC_SQ_SIZE")) != NULL) {
        mvparams.rc_sq_size = atoi(value);
    }
    if((value = getenv("MV_SRQ_SIZE")) != NULL) {
        mvparams.srq_size = atoi(value);
    }

    if ((value = getenv("MV_RC_REL")) != NULL) {
        mvparams.rc_rel = atoi(value);
    }
    if ((value = getenv("MV_UD_REL")) != NULL) {
        mvparams.ud_rel = atoi(value);
    }

    if ((value = getenv("MV_DEVICE")) != NULL) {
        strncpy(mvparams.device_name, value, 32);
    }
    if ((value = getenv("MV_USE_LMC")) != NULL) {
       	mvparams.use_lmc = atoi(value);
    }
    if ((value = getenv("MV_MAX_VL")) != NULL) {
       	mvparams.max_sl = atoi(value);
        if(mvparams.max_sl <= 0) { mvparams.max_sl = 1; }
        else if(mvparams.max_sl > 14) { mvparams.max_sl = 14; }
    }
    if ((value = getenv("MV_PROGRESS_TIMEOUT")) != NULL) {
       	mvparams.progress_timeout = atoi(value);
    }
    if ((value = getenv("MV_RETRY_TIMEOUT")) != NULL) {
        mvparams.retry_usec = atoi(value);
    }
    if ((value = getenv("MV_UD_MAX_INLINE")) != NULL) {
       	mvparams.ud_max_inline = atoi(value);
    }
    if ((value = getenv("MV_RC_MAX_INLINE")) != NULL) {
       	mvparams.rc_max_inline = atoi(value);
    }
    if ((value = getenv("MV_MAX_RETRY_COUNT")) != NULL) {
       	mvparams.max_retry_count = atoi(value);
    }
    if ((value = getenv("MV_MAX_RC_CONNECTIONS")) != NULL) {
       	mvparams.max_rc_connections = atoi(value);
    }
    if ((value = getenv("MV_MAX_RCFP_CONNECTIONS")) != NULL) {
       	mvparams.max_rcfp_connections = atoi(value);
    }
    if ((value = getenv("MV_RC_MESG_SETUP_THRESHOLD")) != NULL) {
       	mvparams.rc_threshold = atoi(value);
    }
    if ((value = getenv("MV_RC_SEND_THRESHOLD")) != NULL) {
       	mvparams.rc_send_threshold = atoi(value);
    }
    if ((value = getenv("MV_RC_SETUP_THRESHOLD")) != NULL) {
       	mvparams.rc_setup_threshold = atoi(value);
    }
    if ((value = getenv("MV_RCFP_THRESHOLD")) != NULL) {
       	mvparams.rcfp_threshold = atoi(value);
    }
    if ((value = getenv("MV_RC_BUF_SIZE")) != NULL) {
       	mvparams.rc_buf_size = atoi(value);
    }
    if ((value = getenv("MV_RCFP_BUF_SIZE")) != NULL) {
       	mvparams.rcfp_buf_size = atoi(value);
    }
    if ((value = getenv("MV_RCFP_BUF_COUNT")) != NULL) {
       	viadev_num_rdma_buffer = atoi(value);
    }
    if ((value = getenv("MV_ACK_AFTER_RECV")) != NULL) {
        mvparams.send_ackafter = atoi(value);
    }
    if ((value = getenv("MV_ACK_AFTER_PROGRESS")) != NULL) {
        mvparams.send_ackafter_progress = atoi(value);
    }
    if ((value = getenv("MV_USE_UD_ZCOPY")) != NULL) {
       	mvparams.use_zcopy = atoi(value);
    }
    if ((value = getenv("MV_UD_ZCOPY_QPS")) != NULL) {
       	mvparams.rndv_qps = atoi(value);
    }
    if ((value = getenv("MV_UD_ZCOPY_THRESHOLD")) != NULL) {
       	mvparams.zcopy_threshold = atoi(value);
    }
#ifndef DISABLE_PTMALLOC
    /* if PTMALLOC is turned off, we set use_dreg_cache=0 above
     * don't allow user to turn it back on here */
    if ((value = getenv("MV_USE_REG_CACHE")) != NULL) {
       	mvparams.use_dreg_cache = atoi(value);
    }
#endif
    if ((value = getenv("MV_USE_HEADER_CACHING")) != NULL) {
       	mvparams.use_header_caching = atoi(value);
    }
    if ((value = getenv("MV_NUM_QPS")) != NULL) {
       	mvparams.num_qps = atoi(value);
    }
    if ((value = getenv("MV_UD_SQ_SIZE")) != NULL) {
               mvparams.ud_sq_size = atoi(value);
    }
    if ((value = getenv("MV_UD_RQ_SIZE")) != NULL) {
               mvparams.ud_rq_size = atoi(value);
    }
    if ((value = getenv("MV_UD_RQ_SIZE_POSTED")) != NULL) {
               mvparams.recvq_size = atoi(value);
    }
    if ((value = getenv("MV_CQ_SIZE")) != NULL) {
               mvparams.cq_size = atoi(value);
    }
    if ((value = getenv("MV_PSN")) != NULL) {
               mvparams.psn = atoi(value);
    }


    if ((value = getenv("MV_RENDEZVOUS_THRESHOLD")) != NULL) {
        mvparams.rendezvous_threshold = atoi(value);
        if(mvparams.rendezvous_threshold <= 0) {
            mvparams.rendezvous_threshold = 1;
        }
    }
    if ((value = getenv("MV_MTU")) != NULL) {
            mvparams.mtu = atoi(value);
    }

#ifdef _SMP_
    /* Run parameter to disable shared memory communication */
    if ( (value = getenv("MV_USE_SHARED_MEM")) != NULL ) {
        disable_shared_mem = !atoi(value);
    }
    /* Should set in KBytes */
    if ( (value = getenv("MV_SMP_EAGERSIZE")) != NULL ) {
        smp_eagersize = atoi(value);
    }
    /* Should set in KBytes */
    if ( (value = getenv("MV_SMPI_LENGTH_QUEUE")) != NULL ) {
        smpi_length_queue = atoi(value);
    }
    if ( (value = getenv("MV_SMP_NUM_SEND_BUFFER")) != NULL ) {
        smp_num_send_buffer = atoi(value);
    }
    if ( (value = getenv("MV_SMP_BATCH_SIZE")) != NULL ) {
        smp_batch_size = atoi(value);
    }

#ifdef _AFFINITY_
    if ((value = getenv("MV_ENABLE_AFFINITY")) != NULL) { 
        viadev_enable_affinity = atoi(value);
    }
   
    /* The new alias will override the old 
     * variable, if defined */

    if ((value = getenv("MV_USE_AFFINITY")) != NULL) {
        viadev_enable_affinity = atoi(value);
    }

    if ((value = getenv("VIADEV_CPU_MAPPING")) != NULL) {
        cpu_mapping = (char *)malloc(sizeof(char) * strlen(value) + 1);
        strcpy(cpu_mapping, value);
    }
#endif
#endif

    D_PRINT("Exiting MV_Init_Params\n");

}

