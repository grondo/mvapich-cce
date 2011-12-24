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

#include <stdio.h>
#include <stdlib.h>
#include "psmpriv.h"
#include "process/pmgr_collective_client.h"

#define GEN_EXIT_ERR     -1     /* general error which forces us to abort */
#define GEN_ASSERT_ERR   -2     /* general assert error */

#define error_abort_all(code, message, args...)  {                  \
    if(NULL == psmdev.my_name) {                                    \
        fprintf(stderr, "[%d] Abort: ", psmdev.me);                 \
    } else {                                                        \
        fprintf(stderr, "[%d:%s] Abort: ", psmdev.me, psmdev.my_name);  \
    }                                                               \
    fprintf(stderr, message, ##args);                               \
    fprintf(stderr, " at line %d in file %s\n", __LINE__, __FILE__);\
    pmgr_abort(code, message, ##args);                                                   \
    exit(code);                                                     \
}



#endif                          /* _VIUTIL_H */
