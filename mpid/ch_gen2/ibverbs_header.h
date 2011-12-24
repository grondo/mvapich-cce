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

#ifndef _IBVERBS_HEADER_H
#define _IBVERBS_HEADER_H


#undef IN
#undef OUT

#include <infiniband/verbs.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>

#include "ibverbs_const.h"
#include "via64.h"

double get_us(void);


#define INVAL_HNDL (0xffffffff)

#define IN
#define OUT

#undef MALLOC
#undef FREE
/* src/env/initutil.c NEW not defined */
#define MALLOC(a)    malloc((size_t)(a))
#define CALLOC(a,b)  calloc((size_t)(a),(size_t)(b))
#define FREE(a)      free((char *)(a))
#define NEW(a)    (a *)MALLOC(sizeof(a))
#define STRDUP(a) 	strdup(a)

#if 0
#define D_PRINT(fmt, args...)   {fprintf(stderr, "[%d][%s:%d]", viadev.me, __FILE__, __LINE__);\
                     fprintf(stderr, fmt, ## args); fflush(stderr);}
#else
#define D_PRINT(fmt, args...)
#endif

#define T_PRINT(fmt, args...)

#if __GNUC__ >= 3
# define VIADEV_LIKELY(x)      __builtin_expect (!!(x), 1)
# define VIADEV_UNLIKELY(x)    __builtin_expect (!!(x), 0)
#else
# define VIADEV_LIKELY(x)      (x)
# define VIADEV_UNLIKELY(x)    (x)
#endif

#if NEED_IBV_STATUS_STR
static inline const char *
ibv_wc_status_str (enum ibv_wc_status status)
{
    switch (status) {
     case IBV_WC_SUCCESS:            return("Success");
     case IBV_WC_LOC_LEN_ERR:        return("Local Length Error");
     case IBV_WC_LOC_QP_OP_ERR:      return("Local QP Operation Error");
     case IBV_WC_LOC_EEC_OP_ERR:     return("Local EE Context Operation Error");
     case IBV_WC_LOC_PROT_ERR:       return("Local Protection Error");
     case IBV_WC_WR_FLUSH_ERR:       return("Work Request Flushed Error");
     case IBV_WC_MW_BIND_ERR:        return("Memory Management Operation Error");
     case IBV_WC_BAD_RESP_ERR:       return("Bad Response Error");
     case IBV_WC_LOC_ACCESS_ERR:     return("Local Access Error");
     case IBV_WC_REM_INV_REQ_ERR:    return("Remote Invalid Request Error");
     case IBV_WC_REM_ACCESS_ERR:     return("Remote Access Error");
     case IBV_WC_REM_OP_ERR:         return("Remote Operation Error");
     case IBV_WC_RETRY_EXC_ERR:      return("Transport Retry Counter Exceeded");
     case IBV_WC_RNR_RETRY_EXC_ERR:  return("RNR Retry Counter Exceeded");
     case IBV_WC_LOC_RDD_VIOL_ERR:   return("Local RDD Violation Error");
     case IBV_WC_REM_INV_RD_REQ_ERR: return("Remote Invalid RD Request");
     case IBV_WC_REM_ABORT_ERR:      return("Aborted Error");
     case IBV_WC_INV_EECN_ERR:       return("Invalid EE Context Number");
     case IBV_WC_INV_EEC_STATE_ERR:  return("Invalid EE Context State");
     case IBV_WC_FATAL_ERR:          return("Fatal Error");
     case IBV_WC_RESP_TIMEOUT_ERR:   return("Response Timeout Error");
     case IBV_WC_GENERAL_ERR:        return("General Error");
    }
    return ("Unknown");
}

static inline const char *
ibv_event_type_str (enum ibv_event_type event_type)
{
    switch (event_type) {
     case IBV_EVENT_CQ_ERR:              return ("CQ Error");
     case IBV_EVENT_QP_FATAL:            return ("QP Fatal");
     case IBV_EVENT_QP_REQ_ERR:          return ("QP Request Error");
     case IBV_EVENT_QP_ACCESS_ERR:       return ("QP Access Error");
     case IBV_EVENT_COMM_EST:            return ("Communication Established");
     case IBV_EVENT_SQ_DRAINED:          return ("SQ Drained");
     case IBV_EVENT_PATH_MIG:            return ("Path Migrated");
     case IBV_EVENT_PATH_MIG_ERR:        return ("Path Migration Request Error");
     case IBV_EVENT_DEVICE_FATAL:        return ("Device Fatal");
     case IBV_EVENT_PORT_ACTIVE:         return ("Port Active");
     case IBV_EVENT_PORT_ERR:            return ("Port Error");
     case IBV_EVENT_LID_CHANGE:          return ("LID Change");
     case IBV_EVENT_PKEY_CHANGE:         return ("PKey Change");
     case IBV_EVENT_SM_CHANGE:           return ("SM Change");
     case IBV_EVENT_SRQ_ERR:             return ("SRQ Error");
     case IBV_EVENT_SRQ_LIMIT_REACHED:   return ("SRQ Limit Reached");
     case IBV_EVENT_QP_LAST_WQE_REACHED: return ("QP Last WQE Reached");
     case IBV_EVENT_CLIENT_REREGISTER:   return ("Client Reregistration");
    }
    return ("Unknown");
}
#endif /* NEED_IBV_STATUS_STR */

#endif /* _IBVERBS_HEADER_H */
