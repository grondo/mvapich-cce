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

#include "mpid.h"
#include "mpid_bind.h"
#include "viadev.h"
#include "sbcnst2.h"
#include "queue.h"
#include "mpid_smpi.h"
#include "process/pmgr_collective_client.h"
#include "viapriv.h"

/* 
 * Global variables used by ADI as well as layers above the ADI so their names
 * should not be changed. 
 */
int MPID_MyWorldSize;
int MPID_MyWorldRank;

/* 
 * Global variables used by the VIA device 
 */

MPID_SBHeader MPIR_rhandles;
MPID_SBHeader MPIR_shandles;

/* This is a prototype for this function used to provide a debugger hook */
void MPIR_Breakpoint (void);

void MPID_Init(int *argc, char ***argv, void *config, int *error_code)
{
    int rc;

    *error_code = MPI_SUCCESS;

    MPID_InitQueue();
    /* Initialize the send/receive handle allocation system */
    /* Use the persistent version of send/receive because it is the largest */
    MPIR_shandles = MPID_SBinit(sizeof(MPIR_PSHANDLE), 100, 100);
    MPIR_rhandles = MPID_SBinit(sizeof(MPIR_PRHANDLE), 100, 100);

    /* initialize VIA, establish communication channels */
    rc = MPID_VIA_Init(argc, argv, &MPID_MyWorldSize, &MPID_MyWorldRank);
    if (rc < 0) {
        *error_code = MPI_ERR_INTERN;
        return;
    }
#ifdef _SMP_
    if (!disable_shared_mem) {
        smpi_init();
    }
#endif
}

void MPID_Abort(struct MPIR_COMMUNICATOR *comm_ptr,
                int code, char *user, char *msg)
{
    char abortString[256];

    fprintf(stderr, "[%d] [%s] %s\n", MPID_MyWorldRank, 
	    user ? user : "", msg ? msg : "Aborting Program!"); 
    fflush(stderr);
    fflush(stdout);

    sprintf(abortString, "MPI_Abort() code: %d, rank %d, %s Aborting program %s",
            code, MPID_MyWorldRank, user ? user : "", msg ? msg : "!" );

    MPIR_debug_abort_string = abortString;
    MPIR_debug_state        = MPIR_DEBUG_ABORTING;
    MPIR_Breakpoint();

    /* flip to a negative code for pmgr to indicate a user abort as opposed to a system abort */
    if (code > 0) { code *= -1; }

    pmgr_abort(code, abortString);
    exit(code);
}

void MPID_End(void)
{

#ifdef _SMP_
    if (!disable_shared_mem) {
        smpi_finish();
    }
#endif
    MPID_VIA_Finalize();
}

void MPID_Version_name(char *name)
{
    sprintf(name, "MVAPICH Gen2/InfiniBand implementation of ADI2 version %s",
            VIADEV_VERSION);
}
