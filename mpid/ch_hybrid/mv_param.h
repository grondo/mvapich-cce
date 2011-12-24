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

#ifndef _MV_PARAM_H
#define _MV_PARAM_H

#include "ibverbs_header.h"

#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))

#define VIADEV_WTICK (0.000010)
#define _AFFINITY_ 1

extern int viadev_num_rdma_buffer;
extern uint8_t mvdev_crc32;

typedef struct {
    uint8_t     conn_type;
    uint8_t     xrc_enabled;

    uint8_t     use_header_caching;
    uint8_t     use_headers;
    uint8_t     use_zcopy;
    uint8_t     use_profile;
    uint8_t     use_lmc;
    uint8_t     use_rc_srq;
    int         max_lmc_total;
    uint8_t     max_rc_perconnection;
    uint16_t    send_window_segments_max;
    uint16_t    recv_window_size_max;
    uint16_t    rndv_qps;
    uint32_t    zcopy_threshold;
    uint32_t    rendezvous_threshold;

    uint32_t    rc_send_threshold;
    uint32_t    rc_setup_threshold;
    uint32_t    rcfp_threshold;
    uint16_t    rcfp_buf_size;

    int         send_ackafter;
    int         send_ackafter_progress;
    int         r3_segments_max;
    int         msrq;
    int         xrcshared;
    char device_name[32];

    uint8_t     rc_rel;
    uint8_t     ud_rel;

    int rc_threshold;
    int max_rc_connections;
    int max_rcfp_connections;
    uint16_t rc_buf_size;
    uint16_t rc_credits;


    int maxtransfersize;

    long        retry_usec;
    long        progress_timeout;

    uint32_t    dreg_cache_limit;
    uint32_t    ndreg_entries;
    uint8_t     use_dreg_cache;

    uint32_t cq_size;
    uint32_t ud_sq_size;
    uint32_t ud_rq_size;
    uint32_t rc_sq_size;
    uint32_t rc_rq_size;

    uint32_t psn;
    int default_port;

    int ud_max_inline;
    int rc_max_inline;

    int recvq_size;
    int credit_preserve;
    int credit_preserve_lazy;

    uint16_t max_retry_count;
    int num_qps;
    int mtu;

    int max_sl;
    int max_ah_total;


    /* RC */ 
    int rc_mtu;
    uint16_t pkey_ix;
    uint8_t min_rnr_timer;
    uint8_t time_out;
    uint8_t static_rate;
    uint8_t retry_count;
    uint8_t rnr_retry;
    uint8_t max_rdma_dst_ops;
    uint8_t qp_ous_rd_atom;
    uint8_t service_level;

    uint16_t srq_size;

    char profile_base_filename[150];

    int      sleep_on_abort;
} mvdev_param_info;

#ifdef _SMP_
/* Run time parameter to disable/enable 
 *  * shared memory communicaiton */
extern int disable_shared_mem;
/* EAGER threshold in SMP path */
extern int                  smp_eagersize;
/* Shared memory buffer pool size */
extern int                  smpi_length_queue;
/*
 *  * Add affinity supprot. Disabled by default.
 *   */
#ifdef _AFFINITY_
extern unsigned int         viadev_enable_affinity;
#endif

#endif

extern mvdev_param_info mvparams;


void MV_Init_Params(int num_proc, int me);

#endif
