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
 * vapi_const.h 
 * 
 * This file contains "tunable parameters" for VAPI. A lot of work 
 * needs to be done to find out what are the right values, and in 
 * fact whether this is the right set of knobs. Many are settable 
 * from the command line and
 * passed to the individual processes as environment variables.
 */

#ifndef _IBVERBS_CONST_H
#define _IBVERBS_CONST_H

/*#define CO_PRINT */
#ifdef CO_PRINT
#define co_print(arg...)                    \
do {                                        \
   printf("[%d]", viadev.me);               \
   printf(arg);                             \
} while (0)
#else
#define co_print(arg...)
#endif

#define VIADEV_DEFAULT_DEVICE                 "mthca0"
#define VIADEV_INVALID_DEVICE                 "nohca"

#define QPLEN_XDR                             (8)

#define VIADEV_MAX_RDMA_SIZE		          (4194304)  


#define VIADEV_DEFAULT_QP_OUS_RD_ATOM     (4)

#define VIADEV_DEFAULT_PSN                    (0)
#define VIADEV_DEFAULT_PKEY_IX                (0) 
#define VIADEV_DEFAULT_PKEY                  (0x0)
#define VIADEV_DEFAULT_MIN_RNR_TIMER          (12)
#define VIADEV_DEFAULT_SERVICE_LEVEL          (0)
#define VIADEV_DEFAULT_TIME_OUT               (20)

#define VIADEV_DEFAULT_STATIC_RATE            (0)

#define VIADEV_DEFAULT_SRC_PATH_BITS          (0)

#define VIADEV_MAX_FAST_EAGER_SIZE            (255)

/* To handle congestion in larger clusters */
#define VIADEV_DEFAULT_RETRY_COUNT            (7)

/* Set to infinite */
#define VIADEV_DEFAULT_RNR_RETRY              (7)

#define VIADEV_DEFAULT_MAX_SG_LIST	          (1)
#define VIADEV_DEFAULT_MAX_RDMA_DST_OPS       (8)

#define HOSTNAME_LEN                          (255)

#define VIADEV_NUM_RDMA_BUFFER                (32)

#define VIADEV_PREPOST_DEPTH 64
#define VIADEV_VBUF_MIN_POOL_SIZE 100
#define VIADEV_PREPOST_RENDEZVOUS_EXTRA VIADEV_PREPOST_DEPTH

#define VIADEV_MAX_SPIN_COUNT                   (5000)

#define VIADEV_PORT_MAX_NUM 2 

#define APM_COUNT 1000
#ifdef MCST_SUPPORT
#define DEFAULT_MAX_CQ_SIZE (400)
#define DEFAULT_MAX_WQE    (200)
#define DEFAULT_FLOW_LABEL      (0)
#define DEFAULT_TRAFFIC_CLASS   (0)
#define DEFAULT_HOP_LIMIT       (63)
#endif

#endif                          /* _IBVERBS_CONST_H */
