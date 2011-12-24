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


#ifndef _PSMPRIV_H
#define _PSMPRIV_H



#include "req.h"
#include "viutil.h"




#define PSMDEV_MAX_EXECNAME (256)

/* One of these structures per VI. Pointer to this
 * structure is also cached on the VI itself for two-way
 * lookup 
 */

enum {
	MPID_PSM_SEND = 1,
	MPID_PSM_RECV = 2,
	MPID_PSM_SEND_RDNV = 3, /*Required to use RDNV Send e.g. for MPI_Ssend*/
};

typedef struct {

    psm_ep_t ep;
    /*End point handle*/
 
    psm_mq_t mq;
    /*Matched Queues Interface*/

    psm_epaddr_t *epaddrs;
    /*Array of endpoint addresses connected to*/

    int np;
    /* number of processes total */
    int me;

    /* is device initialized */
    int initialized;

    /* my process rank */
    char *my_name;
    /* A string equivalent of node name */
    int global_id;
    /* global id of this parallel app */

    /* array of VIs connected to other processes */
    int barrier_id;
    /* Used for PSM barrier operations */

    int *pids;                    /* add for totalview */
    char **processes;
    char execname[PSMDEV_MAX_EXECNAME]; /* add for totalview */

    char device_name[32];         /* Name of the IB device */

    char is_finalized;            /* Flag to indicate if device
                                     is finalized */

} psmdev_info_t;

extern psmdev_info_t psmdev;

/*
 * Function prototypes. All functions used internally by the PSM device. 
 */

void flush_all_messages(void);


/* 
 * on 32 bit machines, we identify (for now) a handle by its address, using the 
 * address for immediate data. 
 * on 64 bit machines, we need to use the pointer index stuff since we don't
 * have 64 bits of immediate data. 
 * We will also need the index stuff when we implement reliability. 
 */



/* 
 * this should be a real function. Need to think carefully about
 * what has to be done. 
 * note model does MPID_UnpackMessageComplete, etc.
 */


#include <unistd.h>

#define SEND_COMPLETE(s) {                                          \
    s->is_complete = 1;                                             \
    if (s->finish != NULL) {                                        \
        s->finish(s);                                               \
    }                                                               \
    if (s->ref_count == 0) {                                        \
        switch (s->handle_type) {                                   \
            case MPIR_SEND:                                         \
            {                                                       \
                MPID_SendFree(s);                                   \
                break;                                              \
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
    if (r->finish != NULL) {                                        \
        r->finish(r);                                               \
    }                                                               \
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
                break;                                              \
            }                                                       \
            default:                                                \
                error_abort_all(GEN_EXIT_ERR, "RECV_COMPLETE invalid type\n");    \
        }                                                           \
    }                                                               \
}

#define MPID_CommInit(oldcomm,newcomm) MPI_SUCCESS
#define MPID_CommFree(oldcomm) MPI_SUCCESS

#endif                          /* _PSMPRIV_H */
