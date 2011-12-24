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

#ifndef _VIADEV_H
#define _VIADEV_H

#include "mpid.h"

#define VIADEV_VERSION "0.9.8"

/* 
 * This file should be included by ADI2 functions. Routines
 * used only internally by the device are in viapriv.h.
 */

int MPID_VIA_Init(int *argc, char ***argv, int *size, int *rank);


void MPID_VIA_Irecv(void *buf,
                    int len,
                    int src_lrank,
                    int tag,
                    int context_id,
                    MPIR_RHANDLE * rhandle, int *error_code);




int MPID_VIA_self_start(void *buf,
                        int len,
                        int src_lrank,
                        int tag, int context_id, MPIR_SHANDLE * shandle);


void MPID_VIA_self_finish(MPIR_RHANDLE * rhandle,
                          MPIR_RHANDLE * unexpected);


int MPID_VIA_rendezvous_start(void *buf,
                              int len,
                              int src_lrank,
                              int tag,
                              int context_id,
                              int dest_grank, MPIR_SHANDLE * shandle);


int MPID_VIA_eager_send(void *buf,
                        int len,
                        int src_lrank,
                        int tag,
                        int context_id, int dest_grank, MPIR_SHANDLE * s);




void MPID_VIA_Finalize(void);

int rank_to_hca_lid(int i);
int rank_to_qp_num(int i);
int ib_init_check(int i, int j);

#endif                          /* _VIADEV_H */
