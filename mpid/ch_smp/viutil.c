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

#include "viutil.h"
#include <sys/time.h>

char viadev_event_code_str[64] = {0};
char viadev_wc_code_str[64] = {0};

double viutil_get_seconds(void)
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return (double) t.tv_sec + ((double) t.tv_usec / (double) 1e6);
}

