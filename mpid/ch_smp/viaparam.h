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
/*
 * viaparam.h 
 * 
 * This file contains "tunable parameters" for MVICH. A lot of work needs to be
 * done to find out what are the right values, and in fact whether this is 
 * the right set of knobs. Many are settable from the command line and
 * passed to the individual processes as environment variables.
 * Default value are set in the viaparam.c
 */

#ifndef _VIAPARAM_H
#define _VIAPARAM_H

#define STRDUP(a)       strdup(a)

/* Use CPU Affinity by default. 
 * It can be disabled/enabled by run time parameter 
 * viadev_enable_affinity */
#define _AFFINITY_ 1

#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))

extern int                  viadev_rendezvous_threshold;

/* enables a process to sleep before calling exit on an abort;
 * negative values cause an infinite sleep time and positive
 * values specify the number of seconds to sleep before exiting */
extern int                  viadev_sleep_on_abort;

typedef struct {
    unsigned int hca_id;
    unsigned int port_id;
} active_ports;

typedef enum {
    UNKNOWN_HCA = 32,
    HCA_ERROR,
    MLX_PCI_X,
    MLX_PCI_EX_SDR,
    MLX_PCI_EX_DDR,
    PATH_HT,
    IBM_EHCA
} viadev_hca_type_t;


#if (defined(_PPC64_) && defined(VIADEV_RGET_SUPPORT))
#error Cannot define both PPC and VIADEV_RGET_SUPPORT
#endif

#ifdef _SMP_
/* Run time parameter to disable/enable 
 * shared memory communicaiton */
extern int disable_shared_mem;
/* EAGER threshold in SMP path */
extern int                  smp_eagersize;
/* Shared memory buffer pool size */
extern int                  smpi_length_queue;
/*
 * Add affinity supprot. Disabled by default.
 */
#ifdef _AFFINITY_
extern unsigned int         viadev_enable_affinity;
extern char *cpu_mapping;
#endif

#endif

/*
 * Why specify clock resolution in a parameters file?
 * It would be best to measure clock resolution, but
 * you are likely to measure slightly different values
 * on different nodes, and this is a Bad Thing. On most
 * recent systems, with Linux, gettimeofday() resolution
 * (MPID_Wtime is based on gettimeofday) is just a few
 * microseconds. The 10 microsecond value here is safe. 
 */

#define VIADEV_WTICK (0.000010)

/* Set default run-time parameters and over-ride with values
 * defined by environment variables passed into this process
 */
void viadev_init_parameters(int num_proc, int me);

#endif                          /* _VIAPARAM_H */
