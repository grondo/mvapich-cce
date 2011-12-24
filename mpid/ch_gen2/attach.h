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

/* This file contains support for bringing processes up stopped, so that
 * a debugger can attach to them     (done for TotalView)
 */


#ifndef _ATTACH_INCLUDE
#define _ATTACH_INCLUDE

#ifndef VOLATILE
#if defined(__STDC__) || defined(__cplusplus)
#define VOLATILE volatile
#else
#define VOLATILE
#endif
#endif

/*************************************************************
 *                DEBUGGING SUPPORT                           *
 **************************************************************/


/* A little struct to hold the target processor name and pid for
 * each process which forms part of the MPI program.
 * We may need to think more about this once we have dynamic processes...
 *
 * DO NOT change the name of this structure or its fields. The debugger knows
 * them, and will be confused if you change them.
 */
typedef struct {
    char *host_name;            /* Something we can pass to inet_addr */
    char *executable_name;      /* The name of the image */
    int pid;                    /* The pid of the process */
} MPIR_PROCDESC;

/* Array of procdescs for debugging purposes */
extern MPIR_PROCDESC *MPIR_proctable;
extern int MPIR_proctable_size;

/* Various global variables which a debugger can use for 
 * 1) finding out what the state of the program is at
 *    the time the magic breakpoint is hit.
 * 2) inform the process that it has been attached to and is
 *    now free to run.
 */
extern VOLATILE int MPIR_debug_state;
extern VOLATILE int MPIR_debug_gate;
extern char *MPIR_debug_abort_string;

/* Cause extra info on internal state
 * to be maintained */
extern int MPIR_being_debugged;

/* Values for the debug_state, this seems to be all 
 * we need at the moment but that may change...  */
#define MPIR_DEBUG_SPAWNED   1
#define MPIR_DEBUG_ABORTING  2

#endif                          /* _ATTACH_INCLUDE */
