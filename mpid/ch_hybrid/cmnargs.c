/*
 *
 * Copyright (C) 1993 University of Chicago
 * Copyright (C) 1993 Mississippi State University
 * Copyright (C) 1999-2001 The Regents of the University of California
 * (through E.O. Lawrence Berkeley National Laboratory), subject to
 * approval by the U.S. Department of Energy.
 *
 * Use of this software is under license. The license agreement is included
 * in the file MVICH_LICENSE.TXT.
 *
 * Developed at Argonne National Laboratory and Mississippi State University as part of MPICH.
 * Modified at Berkeley Lab for MVICH
 *
 */

#include "mpid.h"
#include "cmnargs.h"
void MPID_ArgSqueeze(int *Argc, char **argv)
{
    int argc, i, j;

    /* Compress out the eliminated args */
    argc = *Argc;
    j    = 0;
    i    = 0;

    while (j < argc) {
        while (argv[j] == 0 && j < argc)
            j++;
        if (j < argc)
            argv[i++] = argv[j++];
    }
    /* Back off the last value if it is null */
    if (!argv[i-1])
        i--;
    *Argc = i;
}
