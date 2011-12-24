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
#ifndef _PSMPARAM_H
#define _PSMPARAM_H


/*
 * Why specify clock resolution in a parameters file?
 * It would be best to measure clock resolution, but
 * you are likely to measure slightly different values
 * on different nodes, and this is a Bad Thing. On most
 * recent systems, with Linux, gettimeofday() resolution
 * (MPID_Wtime is based on gettimeofday) is just a few
 * microseconds. The 10 microsecond value here is safe. 
 */

#define PSMDEV_WTICK (0.000010)

extern void viadev_init_parameters();

#ifdef _SMP_
#ifdef _AFFINITY_
extern unsigned int         viadev_enable_affinity;
extern char *cpu_mapping;
#endif
#endif

#endif                          /* _PSMPARAM_H */
