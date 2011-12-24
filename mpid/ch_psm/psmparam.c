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
 * ==============================================================
 * Initialize global parameter variables to default values
 * ==============================================================
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef _SMP_
#ifdef _AFFINITY_
/* Affinity is disabled by default */
unsigned int viadev_enable_affinity=0;
char *cpu_mapping = NULL;
#endif
#endif

int               viadev_sleep_on_abort = 0; /* disabled by default */

void viadev_init_parameters()
{
    char *value;

#ifdef _SMP_
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

    if ((value = getenv("VIADEV_SLEEP_ON_ABORT")) != NULL) {
        viadev_sleep_on_abort = atoi(value);
    }
}
