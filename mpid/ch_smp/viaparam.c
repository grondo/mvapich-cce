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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "viaparam.h"
#include "viapacket.h"
#include "mpid_smpi.h"

/* The message size (in bytes) at which we switch from an EAGER
 * protocol to the three-way RENDEZVOUS protocol.
 * In reality, smaller messages might be sent using R3
 * rather than EAGER if remote credits are low.
 *
 * This value will be set in viadev_set_default_parameters()
 */
int viadev_rendezvous_threshold = 0;

#ifdef _SMP_
int disable_shared_mem = 0; 
int smp_num_send_buffer = SMP_NUM_SEND_BUFFER;
int smp_batch_size = SMP_BATCH_SIZE;
#ifdef _AFFINITY_
/* Affinity is disabled by default */
unsigned int viadev_enable_affinity=0;
char *cpu_mapping = NULL;
#endif 
#endif

void viadev_init_parameters(int num_proc, int me)
{
    char *value;
#ifdef _SMP_
    /* Run parameter to disable shared memory communication */
    if ( (value = getenv("VIADEV_USE_SHARED_MEM")) != NULL ) {
        disable_shared_mem = !atoi(value);
    }
    /* Should set in KBytes */
    if ( (value = getenv("VIADEV_SMP_EAGERSIZE")) != NULL ) {
        smp_eagersize = atoi(value);
    }
    /* Should set in KBytes */
    if ( (value = getenv("VIADEV_SMPI_LENGTH_QUEUE")) != NULL ) {
        smpi_length_queue = atoi(value);
    }
    if ( (value = getenv("VIADEV_SMP_NUM_SEND_BUFFER")) != NULL ) {
        smp_num_send_buffer = atoi(value);
    }
    if ( (value = getenv("VIADEV_SMP_BATCH_SIZE")) != NULL ) {
        smp_batch_size = atoi(value);
    }

#ifdef _AFFINITY_
    if ((value = getenv("VIADEV_ENABLE_AFFINITY")) != NULL) {
	    viadev_enable_affinity = atoi(value);
    }
   
    /* The new alias will override the old 
     * variable, if defined */
    
    if ((value = getenv("VIADEV_USE_AFFINITY")) != NULL) {
	    viadev_enable_affinity = atoi(value);
    }

    if ((value = getenv("VIADEV_CPU_MAPPING")) != NULL) {
	cpu_mapping = (char *)malloc(sizeof(char) * strlen(value) + 1);
        strcpy(cpu_mapping, value);
    }
#endif
#endif
}

