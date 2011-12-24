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

void MPID_PackMessage(void *src,
                      int count,
                      struct MPIR_DATATYPE *dtype_ptr,
                      struct MPIR_COMMUNICATOR *comm_ptr,
                      int dest_grank,
                      MPID_Msgrep_t msgrep,
                      MPID_Msg_pack_t msgact,
                      void **mybuf, int *mylen, int *error_code);

void MPID_UnpackMessageSetup(int count,
                             struct MPIR_DATATYPE *dtype_ptr,
                             struct MPIR_COMMUNICATOR *comm_ptr,
                             int dest_grank,
                             MPID_Msgrep_t msgrep,
                             void **mybuf, int *mylen, int *error_code);


void *MPID_Adjust_Bottom(void *buf, struct MPIR_DATATYPE *type);
int Is_MPI_Bottom(void *buf, int len, struct MPIR_DATATYPE *type);

/* XXX KLUDGE ALERT! 
 * This function is defined in mpipt2pt.h for some reason. 
 * We would like to be able to build the psm device cleanly, 
 * with all compiler warnings turned on. We get a warning 
 * because
 * MPIR_Errors_are_fatal and several other functions are
 * defined twice.
 * So the ones we need are defined here and we don't include
 * the broken mpipt2pt.h
 */
int MPIR_Type_free(struct MPIR_DATATYPE **);

/* needed to increment refernece count  SAF */
struct MPIR_DATATYPE *MPIR_Type_dup(struct MPIR_DATATYPE *);

#include "mpidmpi.h"            /* MPIR_Unpack and MPIR_Pack2 */
