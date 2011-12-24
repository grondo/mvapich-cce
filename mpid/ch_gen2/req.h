/*
 *
 * Copyright (C) 1993 University of Chicago
 * Copyright (C) 1993 Mississippi State University
 * Copyright (C) 1999-2001 The Regents of the University of California
 * (through E.O. Lawrence Berkeley National Laboratory), subject to
 * approval by the U.S. Department of Energy.
 *
 * Use of this software is under license. The license agreement is included
 * in the file MVICH_LICENSE.TXT.
 *
 * Developed at Argonne National Laboratory and Mississippi State University as part of MPICH.
 * Modified at Berkeley Lab for MVICH
 *
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

#ifndef _REQ_H
#define _REQ_H


#ifndef MPIR_REQUEST_COOKIE

#ifndef ANSI_ARGS
#if defined(__STDC__) || defined(__cplusplus) || defined(HAVE_PROTOTYPES)
#define ANSI_ARGS(a) a
#else
#define ANSI_ARGS(a) ()
#endif
#endif

#include "datatype.h"

/* xxx definition of request_id_t lives here. Best to move 
 * definition because this is a lot of code to include - wcs
 */
#include "viapacket.h"

/* 
 * we use void* for a pointer to a via_connection_t and 
 * a vbuf and a dreg_entry. This is to avoid circular include files, but
 * but there should be a better way to do things that
 * makes the type explicit but doesn't require knowledge
 * of the internals of these structures for everyone who 
 * includes this file. 
 * A nasty side effect is that when we use these members
 * of send and receive request handles, we have to explicitly
 * cast them to the right type. 
 */

/*
 * VIP_MEM_HANDLE is a 32-bit quantity that may be a pointer
 * on some machines and an integer on others. Best not to 
 * use void* as above. Plus with vipl.h we don't have the 
 * problem of circular include files. 
 */

#include <ibverbs_header.h>


/* MPIR_COMMON is the is common to all handle structures.  */

/* User not yet supported */
typedef enum {
    MPIR_SEND,
    MPIR_RECV,
    MPIR_PERSISTENT_SEND,
    MPIR_PERSISTENT_RECV
} MPIR_OPTYPE;

#ifdef VIADEV_SEND_CANCEL
/* send cancel states */
typedef enum {
    MPID_SC_NONE,               /* no send cancel */
    MPID_SC_STALL,              /* Stall sends on sending side, if possible */
    MPID_SC_KILLED
} MPID_SEND_CANCEL;
#endif

#define MPIR_REQUEST_COOKIE 0xe0a1beaf

typedef struct _MPIR_COMMON MPIR_COMMON;

struct _MPIR_COMMON {
    MPIR_OPTYPE handle_type;
    /* Cookie to help detect valid item */
    MPIR_COOKIE int is_complete;

    /* Used when mapping to/from indices */
    int self_index;

    /* Used to handle freed (by user) but not complete */
    int ref_count;

    /* This is a callback mechanism for when we need to use an
     * MPI-managed buffer. The cases I know about are:
     * 1. for IsendDatatype, MPID must allocate
     *    a buffer to hold a contiguous version and copies the user buffer
     *    into this contiguous buffer before sending. The buffer must be freed
     *    when the request is complete. The callback is just a FREE. 
     *
     * 2. for IrecvDatatype, MPID allocates a contiguous buffer. When the receive
     *    completes, data must be unpacked from the MPI buffer into the user
     *    buffer. The callback is both a copy and a FREE. 
     *
     * xxx use void* instead of MPIR_HANDLE* because I can't
     * figure out how do this correctly. Doing 
     * typedef union _MPIR_HANDLE MPIR_HANDLE above leads to an 
     * incomplete type error at compile time.  - wcs
     *
     */
    int (*finish) (void *);
} _MPIR_COMMON;

typedef struct _MPIR_SHANDLE MPIR_SHANDLE;
struct _MPIR_SHANDLE {
    MPIR_OPTYPE handle_type;

    /* Cookie to help detect valid item */
     MPIR_COOKIE
        /* Indicates if complete */
    int is_complete;

    /* Used when mapping to/from pointer indices */
    int self_index;

    /* Used to handle freed (by user) but not complete */
    int ref_count;

    /* see comments under MPIR_COMMON */
    int (*finish) (void *);
    MPI_Status s;
    int is_cancelled;
    int cancel_complete;

    int bytes_total;
    int bytes_sent;

    /* nearly_complete is a hack used only in rendezvous sends (all types).
     * the problem is that we need to have a way to indicate that all packets
     * for a message have been posted, so that a send request can be taken off
     * of the pushlist. We can't just check that bytes_sent == bytes_total
     * because in the case of a zero-length message (e.g. for ssend) this
     * won't be correct. We can't use is_complete because this indicates
     * that the message is complete at the MPI level, which is not true
     * after packets have been posted but before they have been completed
     */

    int nearly_complete;

    /* local address info */
    void *local_address;
    void *dreg_entry;
    void *remote_address;
    uint32_t remote_memhandle_rkey;

    /* Filled in when OTS is received */
    request_id_t receive_id;

#ifdef _SMP_
    struct {
        void *ptr;
        unsigned long current_expected;
        unsigned long current_offset;
        unsigned long current_done;
        unsigned int rendez_vous;
    } smp;
#endif
    int partner;


    /* We keep a chain of send handles attached to each VI.
     * When credit comes in on a particular VI, we try to make progress 
     * on pending sends.
     */
    MPIR_SHANDLE *nexthandle;
    void *connection;

    /* needed by bsend utilities */
    void *bsend;
    /* Pointer to structure managed for 
       buffered sends */

    /* this field is set by bsend_init but apparently never used. 
     * put it here for now so MPICH can compile without modification, 
     * but deal with it later. 
     */
    void *start;

    /* added to explicitly define protocol we are using */
    int protocol;
    /* pasha, added pointer to the vbuf, in NR as pointer to rndv start message */
    void *rndv_start;

#ifdef VIADEV_SEND_CANCEL
    /* state of cancel operation */
    MPID_SEND_CANCEL send_cancel;
#endif
};

/* 
 * A receive request is VERY different from a send.  We need to 
 * keep the information about the message we want in the request,
 * as well as how to accept it.
 */
typedef struct _MPIR_RHANDLE MPIR_RHANDLE;
struct _MPIR_RHANDLE {
    MPIR_OPTYPE handle_type;
    MPIR_COOKIE int is_complete;
    int self_index;
    int ref_count;
    int (*finish) (void *);
    MPI_Status s;

    /* The following describes the user buffer to be received */
    void *start;

    /* dreg entry corresponding to *start above, if memory is registered */
    void *dreg_entry;

    /* Normally the start entry above is a pointer to the user's buffer. 
     * For datatype receives, however, it will be a pointer to an 
     * MPI buffer. In this case, the finish() function must unpack
     * the MPI buffer into the user buffer. Here is where we keep
     * a pointer to it. 
     */
    /*void *user_buf; change to buf to make totalview compatible */
    void *buf;

    /* After post, maximum buffer size; after match, actual data size */
    /*int bytes_as_contig; changed to len for totalview */
    int len;

    /* This information used only by MPID datatype routines.
     * datatype is a pointer to the datatype used in the receive. 
     * count is the count used in the receive, which we have to remember
     */

    int count;
    struct MPIR_DATATYPE *datatype;

    /* This is really a (viapriv_connection_t *). A pointer
     * for all connection state associated with a message
     */
    void *connection;

    /* bookeeping information to keep track of the progress of 
     * a message transfer 
     */
    int vbufs_expected;
    int vbufs_received;
    int bytes_copied_to_user;

    /* An ID for the corresponding send request on the other side. 
     * We use this in rendezvous replies to indicate which transfer
     * we're talking about. 
     */

    request_id_t send_id;

    /* These are used for rget and rput protocol. Filled in when
     * rendezvous start message is received.
     */
    void *remote_address;
    uint32_t remote_memhandle_rkey;

#ifdef _SMP_
    char *unex_buf;
    int from;
    struct {
        char *netbuf;
        unsigned long current_expected;
        unsigned long current_offset;
        unsigned long current_done;
        unsigned int index;
    } smp;
#endif

   int bytes_sent;

    /* To handle incoming eager data we attach a string of vbufs to 
     * the receive handle. The vbufs are chained through the 
     * Next field in the descriptor. 
     */
    void *vbuf_head;
    void *vbuf_tail;

    /* after we have matched an incoming message with a posted receive, 
     * we know what the protocol will be. Store it here so we don't
     * have to recompute. 
     */
    int protocol;

    /* used to indicate if receive can be cancelled or not */
    int can_cancel;

    /* set to nonzero after rendezvous reply has been send. Used
     * only for sanity checking. 
     */
    int replied;


    char * coalesce_data_buf;

    /* NR */
    struct  _MPIR_RHANDLE *next;
    struct  _MPIR_RHANDLE *prev;
    packet_sequence_t sn; /* used for NR only */
    void *fin; /* pointer to vbuf that used for fin message, NR only */
    int was_retransmitted;  /* 0 - false, 1 - true , NR only */
};

typedef struct {
    MPIR_RHANDLE rhandle;
    int active;
    int perm_tag, perm_source, perm_count;
    void *perm_buf;
    struct MPIR_DATATYPE *perm_datatype;
    struct MPIR_COMMUNICATOR *perm_comm;
} MPIR_PRHANDLE;

typedef struct {
    MPIR_SHANDLE shandle;
    int active;
    int perm_tag, perm_dest, perm_count;
    void *perm_buf;
    struct MPIR_DATATYPE *perm_datatype;
    struct MPIR_COMMUNICATOR *perm_comm;
    void (*send) ANSI_ARGS((struct MPIR_COMMUNICATOR *, void *, int,
                            struct MPIR_DATATYPE *,
                            int, int, int, int, MPI_Request, int *));
    /* IsendDatatype, IssendDatatype, Ibsend, IrsendDatatype */
} MPIR_PSHANDLE;

#define MPIR_HANDLES_DEFINED

union MPIR_HANDLE {
    MPIR_OPTYPE handle_type;
    MPIR_COMMON chandle;        /* common fields */
    MPIR_SHANDLE shandle;
    MPIR_RHANDLE rhandle;
    MPIR_PSHANDLE persistent_shandle;
    MPIR_PRHANDLE persistent_rhandle;
};

#ifdef STDC_HEADERS
/* Prototype for memset() */
#include <string.h>
#elif defined(HAVE_STRING_H)
#include <string.h>
#elif defined(HAVE_MEMORY_H)
#include <memory.h>
#endif

#ifdef DEBUG_INIT_MEM


#define MPID_Request_init( ptr, in_type ) {                         \
    memset(ptr,0,sizeof(*(ptr)));                                   \
    (ptr)->handle_type = in_type;                                   \
    (ptr)->ref_count = 1;                                           \
    (ptr)->finish = NULL;                                           \
    if (in_type == MPIR_SEND || in_type == MPIR_PERSISTENT_SEND) {  \
        ((MPIR_SHANDLE *)(ptr))->dreg_entry = NULL;                 \
    }                                                               \
    if (in_type == MPIR_RECV || in_type == MPIR_PERSISTENT_RECV) {  \
        ((MPIR_RHANDLE *)(ptr))->dreg_entry = NULL;                 \
    }                                                               \
    MPIR_SET_COOKIE((ptr),MPIR_REQUEST_COOKIE);                     \
}

#else
        /* For now, turn on deliberate garbaging of memory to catch problems */
        /* We should do this, but I want to get the release out */
        /*                    memset(ptr,0xfc,sizeof(*(ptr))); */


#define MPID_Request_init( ptr, in_type ) {                         \
    memset(ptr,0,sizeof(*(ptr)));                                   \
    (ptr)->handle_type = in_type;                                   \
    (ptr)->ref_count = 1;                                           \
    (ptr)->finish = NULL;                                           \
    if (in_type == MPIR_SEND || in_type == MPIR_PERSISTENT_SEND) {  \
        ((MPIR_SHANDLE *)(ptr))->dreg_entry = NULL;                 \
    }                                                               \
    if (in_type == MPIR_RECV || in_type == MPIR_PERSISTENT_RECV) {  \
        ((MPIR_RHANDLE *)(ptr))->dreg_entry = NULL;                 \
    }                                                               \
    MPIR_SET_COOKIE((ptr),MPIR_REQUEST_COOKIE);                     \
}

#endif                          /* DEBUG_INIT_MEM */

#endif                          /* MPIR_REQUEST_COOKIE */

#define MPID_SendRequestCancelled(r) ((r)->shandle.s.MPI_TAG == MPIR_MSG_CANCELLED)
#define MPID_SendRequestErrval(r) ((r)->s.MPI_ERROR)

#endif                          /* _REQ_H */
