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


#ifndef _VIUTIL_H
#define _VIUTIL_H

#include <string.h>
#include <errno.h>
#include <stdio.h>
#include "viapriv.h"
#include "process/pmgr_collective_client.h"

#define GEN_EXIT_ERR     -1     /* general error which forces us to abort */
#define GEN_ASSERT_ERR   -2     /* general assert error */
#define IBV_RETURN_ERR   -3     /* ibverbs funtion return error */
#define IBV_STATUS_ERR   -4     /* ibverbs funtion status error */
#define HOSTNAME_LEN                          (255)

#define D_PRINT(fmt, args...)

#define error_abort_all(code, args...) { \
    pmgr_abort(code, ##args); \
}

extern char viadev_event_code_str[64];
extern char viadev_wc_code_str[64];

double viutil_get_seconds(void);
char *event_code_to_str(int);
char* wc_code_to_str(int code);

#endif                          /* _VIUTIL_H */
