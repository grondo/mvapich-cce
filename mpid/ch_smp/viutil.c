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

static int error_get_name(char* buf, int size)
{
    int n = 0;

    /* build the name for this process format as
     *   [rank X: host Y: pid Z] */
    if (viadev.my_name != NULL && viadev.pids != NULL && viadev.me >= 0) {
        n = snprintf(buf, size, "[rank %d: host %s: pid %d]",
                viadev.me, viadev.my_name, viadev.pids[viadev.me]
        );
    } else if (viadev.my_name != NULL) {
        n = snprintf(buf, size, "[rank %d: host %s]",
                viadev.me, viadev.my_name
        );
    } else if (viadev.pids != NULL && viadev.me >= 0) {
        n = snprintf(buf, size, "[rank %d: pid %d]",
                viadev.me, viadev.pids[viadev.me]
        );
    } else {
        n = snprintf(buf, size, "[rank %d]",
                viadev.me
        );
    }

    /* if the name didn't fit in the buffer, return 1 for failure
     * otherwise, return 0 for success */
    if (n >= size) {
        return 1;
    }
    return 0;
}

/* this is called from the error_abort_all macro;
 * it's easier to code and debug this logic by placing it in a function,
 * and it also provides a point where someone can place a breakpoint */
void error_abort_debug()
{
    int loop = 1;
    int sleep_secs = 60;

    /* enable a process to sleep for some time before it calls pmgr_abort and exit */
    if (viadev_sleep_on_abort != 0) {
        /* negative values imply an infinite amount of time,
         * positive values specify the number of seconds to sleep for */
        if (viadev_sleep_on_abort > 0) {
          loop = 0;
          sleep_secs = viadev_sleep_on_abort;
        }

        /* get the name of this process */
        char name[256];
        error_get_name(name, sizeof(name));

        /* print message informing user how long we'll sleep for */
        if (loop == 1) {
            fprintf(stderr, "%s Sleeping in infinite loop\n", name);
        } else {
            fprintf(stderr, "%s Sleeping for %d seconds\n", name, sleep_secs);
        }

        /* flush stdout and stderr to force out any message code may have buffered */
        fflush(stdout);
        fflush(stderr);

        /* now sleep (and possibly loop forever) */
        do {
          sleep(sleep_secs);
        } while (loop);
    }

    return;
}

double viutil_get_seconds(void)
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return (double) t.tv_sec + ((double) t.tv_usec / (double) 1e6);
}

