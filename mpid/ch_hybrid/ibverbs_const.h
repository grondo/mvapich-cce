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

#define MVDEV_DEFAULT_DEVICE                 "mthca0"
#define MVDEV_INVALID_DEVICE                 "nohca"

#define HOSTNAME_LEN                          (255)

#endif                          /* _IBVERBS_CONST_H */
