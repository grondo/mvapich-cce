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
/*
 * viaparam.h 
 * 
 * This file contains "tunable parameters" for MVICH. A lot of work needs to be
 * done to find out what are the right values, and in fact whether this is 
 * the right set of knobs. Many are settable from the command line and
 * passed to the individual processes as environment variables.
 * Default value are set in the viaparam.c
 */

#ifndef _VIAPARAM_H
#define _VIAPARAM_H

#include "ibverbs_header.h"

/* Use CPU Affinity by default. 
 * It can be disabled/enabled by run time parameter 
 * viadev_enable_affinity */
#define _AFFINITY_ 1

/* 
 * If SRQ is defined, by default we will
 * use the Adaptive RDMA mechanism
 */
#ifdef MEMORY_SCALE

#define ADAPTIVE_RDMA_FAST_PATH

#endif /* MEMORY_SCALE */

#ifdef MEMORY_RELIABLE

/* Cannot enable RDMA when memory-to-memory
 * reliability is enabled */
#undef ADAPTIVE_RDMA_FAST_PATH

#endif

#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))

/* the following are a list of globally defined variables to control
 * the behaviour and limits within MVICH.  Many are run-time setable.
 * See viaparam.c for details.
 */

extern int                  viadev_default_mtu;
extern unsigned long        viadev_max_rdma_size;
extern uint8_t              viadev_default_qp_ous_rd_atom;
extern uint32_t             viadev_default_psn;
extern uint16_t             viadev_default_pkey;
extern uint16_t             viadev_default_pkey_ix;
extern uint8_t              viadev_default_min_rnr_timer;
extern uint8_t              viadev_default_service_level;
extern uint8_t              viadev_default_time_out;
extern uint8_t              viadev_default_static_rate;
extern uint8_t              viadev_default_src_path_bits;
extern int                  viadev_max_fast_eager_size;
extern uint8_t              viadev_default_retry_count;
extern uint8_t              viadev_default_rnr_retry;
extern uint32_t             viadev_default_max_sg_list;
extern uint8_t              viadev_default_max_rdma_dst_ops;

extern unsigned long        viadev_sq_size;
extern unsigned long        viadev_coalesce_threshold;
extern unsigned long        viadev_coalesce_threshold_size;
extern unsigned long        viadev_rq_size;
extern unsigned long        viadev_cq_size;
extern int                  viadev_default_port;
#ifdef XRC
extern unsigned int         viadev_default_hca;
#endif 
extern int                  viadev_max_ports;

extern int                  viadev_vbuf_total_size;

extern unsigned int         viadev_ndreg_entries;
extern int                  viadev_use_dreg_cache;

extern int                  viadev_num_rdma_buffer;
extern uint32_t             viadev_max_inline_size;
extern int                  viadev_no_inline_threshold;

#ifdef ADAPTIVE_RDMA_FAST_PATH
extern int                  viadev_rdma_eager_threshold;
extern int                  viadev_rdma_eager_limit;
extern int                  viadev_adaptive_enable_limit;
#endif

extern uint32_t             viadev_srq_alloc_size;
extern uint32_t             viadev_srq_fill_size;
extern uint32_t             viadev_srq_limit;
extern int                  viadev_max_r3_pending_data;
extern uint32_t             viadev_srq_zero_post_max;
extern int                  viadev_use_srq;
#ifdef XRC
extern int                  viadev_use_xrc;
#endif /* XRC */

extern uint32_t             viadev_use_eager_coalesce;
extern uint32_t             viadev_use_eager_coalesce_limited;
extern int                  viadev_progress_threshold;
extern int                  viadev_eager_ok_threshold;

extern int                  viadev_rndv_protocol;
extern int                  viadev_async_progress;
extern int                  viadev_async_schedule_policy;
extern int                  viadev_r3_nocache_threshold;
extern int                  viadev_r3_threshold;
extern int                  viadev_on_demand_threshold;
extern int                  viadev_use_on_demand;

extern int                  viadev_prepost_depth;
extern int                  viadev_prepost_threshold;
extern int                  viadev_initial_prepost_depth;
extern int                  viadev_initial_credits;
extern int                  viadev_vbuf_pool_size;
extern int                  viadev_vbuf_secondary_pool_size;
extern int                  viadev_vbuf_max;
extern int                  viadev_rendezvous_threshold;
extern int                  viadev_prepost_rendezvous_extra;
extern int                  viadev_prepost_noop_extra;
extern int                  viadev_credit_preserve;
extern int                  viadev_credit_notify_threshold;
extern int                  viadev_dynamic_credit_threshold;
extern unsigned long        viadev_pt2pt_failover;
extern unsigned long int    viadev_dreg_cache_limit;
extern int                  mpir_alltoall_medium_msg;
extern int                  mpir_alltoall_short_msg;

extern int                  disable_lmc;
extern unsigned long long   viadev_max_spin_count;
extern int                  viadev_use_blocking;

extern uint32_t             viadev_debug_level;

/* enables a process to sleep before calling exit on an abort;
 * negative values cause an infinite sleep time and positive
 * values specify the number of seconds to sleep before exiting */
extern int                  viadev_sleep_on_abort;

extern unsigned int         viadev_multiport;
extern unsigned int         viadev_multihca;
extern unsigned int         num_hcas;

extern unsigned int         viadev_use_apm;
extern unsigned int         viadev_use_apm_test;
extern unsigned int         apm_count;
extern unsigned int         viadev_nfr_ack_threshold;
extern unsigned int         viadev_eth_over_ib;
extern unsigned int         viadev_use_nfr;

typedef struct {
    unsigned int hca_id;
    unsigned int port_id;
} active_ports;

typedef enum {
    UNKNOWN_HCA = 32,
    HCA_ERROR,
    MLX_PCI_X_GEN,
    MLX_PCI_EX_SDR_LION_GEN,
    MLX_PCI_EX_DDR_LION_GEN,
    CONNECTX_SDR,
    CONNECTX_DDR,
    CONNECTX_QDR,
    PATH_HT,
    RDMAOE,
    IBM_EHCA
} viadev_hca_type_t;

#if (defined(_PPC64_) && defined(VIADEV_RGET_SUPPORT))
#error Cannot define both PPC and VIADEV_RGET_SUPPORT
#endif

#if (defined(MEMORY_RELIABLE) && defined(VIADEV_RGET_SUPPORT))
#error Please use VIADEV_RPUT_SUPPORT with MEMORY_RELIABLE
#endif

#ifdef _SMP_
/* Run time parameter to disable/enable 
 * shared memory communicaiton */
extern int disable_shared_mem;
/* EAGER threshold in SMP path */
extern int                  smp_eagersize;
/* Shared memory buffer pool size */
extern int                  smpi_length_queue;
/*
 * Add affinity supprot. Disabled by default.
 */
#ifdef _AFFINITY_
extern unsigned int         viadev_enable_affinity;
extern char *cpu_mapping;
#endif

#endif

/*
 * Why specify clock resolution in a parameters file?
 * It would be best to measure clock resolution, but
 * you are likely to measure slightly different values
 * on different nodes, and this is a Bad Thing. On most
 * recent systems, with Linux, gettimeofday() resolution
 * (MPID_Wtime is based on gettimeofday) is just a few
 * microseconds. The 10 microsecond value here is safe. 
 */

#define VIADEV_WTICK (0.000010)

/* Set default run-time parameters and over-ride with values
 * defined by environment variables passed into this process
 */
void viadev_init_hca_parameters();
void viadev_init_parameters(int num_proc, int me);
void dump_param_values(FILE * fd, int me);
void viadev_set_default_parameters (int nprocs, int me, int do_autodetect, int hca_type);
int get_hca_type(struct ibv_device *dev,struct ibv_context *ctx);

#endif                          /* _VIAPARAM_H */
