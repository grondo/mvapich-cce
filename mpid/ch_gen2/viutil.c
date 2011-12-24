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


#include "ibverbs_header.h"
#include "viutil.h"
#include <sys/time.h>
#include <stdio.h>

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

char* wc_code_to_str(int code)
{
    char *str;

    switch(code) {
        case IBV_WC_SUCCESS:
            str = "IBV_WC_SUCCESS\0";
            break;
        case IBV_WC_LOC_LEN_ERR:
            str = "IBV_WC_LOC_LEN_ERR\0";
            break;
        case IBV_WC_LOC_QP_OP_ERR:
            str = "IBV_WC_LOC_QP_OP_ERR\0";
            break;
        case IBV_WC_LOC_EEC_OP_ERR:
            str = "IBV_WC_LOC_EEC_OP_ERR\0";
            break;
        case IBV_WC_LOC_PROT_ERR:
            str = "IBV_WC_LOC_PROT_ERR\0";
            break;
        case IBV_WC_WR_FLUSH_ERR:
            str = "IBV_WC_WR_FLUSH_ERR\0";
            break;
        case IBV_WC_MW_BIND_ERR:
            str = "IBV_WC_MW_BIND_ERR\0";
            break;
        case IBV_WC_BAD_RESP_ERR:
            str = "IBV_WC_BAD_RESP_ERR\0";
            break;
        case IBV_WC_LOC_ACCESS_ERR:
            str = "IBV_WC_LOC_ACCESS_ERR\0";
            break;
        case IBV_WC_REM_INV_REQ_ERR:
            str = "IBV_WC_REM_INV_REQ_ERR\0";
            break;
        case IBV_WC_REM_ACCESS_ERR:
            str = "IBV_WC_REM_ACCESS_ERR\0";
            break;
        case IBV_WC_REM_OP_ERR:
            str = "IBV_WC_REM_OP_ERR\0";
            break;
        case IBV_WC_RETRY_EXC_ERR:
            str = "IBV_WC_RETRY_EXC_ERR\0";
            break;
        case IBV_WC_RNR_RETRY_EXC_ERR:
            str = "IBV_WC_RNR_RETRY_EXC_ERR\0";
            break;
        case IBV_WC_LOC_RDD_VIOL_ERR:
            str = "IBV_WC_LOC_RDD_VIOL_ERR\0";
            break;
        case IBV_WC_REM_INV_RD_REQ_ERR:
            str = "IBV_WC_REM_INV_RD_REQ_ERR\0";
            break;
        case IBV_WC_REM_ABORT_ERR:
            str = "IBV_WC_REM_ABORT_ERR\0";
            break;
        case IBV_WC_INV_EECN_ERR:
            str = "IBV_WC_INV_EECN_ERR\0";
            break;
        case IBV_WC_INV_EEC_STATE_ERR:
            str = "IBV_WC_INV_EEC_STATE_ERR\0";
            break;
        case IBV_WC_FATAL_ERR:
            str = "IBV_WC_FATAL_ERR\0";
            break;
        case IBV_WC_RESP_TIMEOUT_ERR:
            str = "IBV_WC_RESP_TIMEOUT_ERR\0";
            break;
        case IBV_WC_GENERAL_ERR:
            str = "IBV_WC_GENERAL_ERR\0";
            break;
        default:
            str = "UNKNOWN\0";
    }

    strncpy(viadev_wc_code_str, str, 64);

    return viadev_wc_code_str;
}

char* event_code_to_str(int code)
{
    char *str;

    switch(code){
        case IBV_EVENT_CQ_ERR:
            str = "IBV_EVENT_CQ_ERR\0";
            break;
        case IBV_EVENT_QP_FATAL:
            str = "IBV_EVENT_QP_FATAL\0";
            break;
        case IBV_EVENT_QP_REQ_ERR:
            str = "IBV_EVENT_QP_REQ_ERR\0";
            break;
        case IBV_EVENT_QP_ACCESS_ERR:
            str = "IBV_EVENT_QP_ACCESS_ERR\0";
            break;
        case IBV_EVENT_COMM_EST:
            str = "IBV_EVENT_COMM_EST\0";
            break;
        case IBV_EVENT_SQ_DRAINED:
            str = "IBV_EVENT_SQ_DRAINED\0";
            break;
        case IBV_EVENT_PATH_MIG:
            str = "IBV_EVENT_PATH_MIG\0";
            break;
        case IBV_EVENT_PATH_MIG_ERR:
            str = "IBV_EVENT_PATH_MIG_ERR\0";
            break;
        case IBV_EVENT_DEVICE_FATAL:
            str = "IBV_EVENT_DEVICE_FATAL\0";
            break;
        case IBV_EVENT_PORT_ACTIVE:
            str = "IBV_EVENT_PORT_ACTIVE\0";
            break;
        case IBV_EVENT_PORT_ERR:
            str = "IBV_EVENT_PORT_ERR\0";
            break;
        case IBV_EVENT_LID_CHANGE:
            str = "IBV_EVENT_LID_CHANGE\0";
            break;
        case IBV_EVENT_PKEY_CHANGE:
            str = "IBV_EVENT_PKEY_CHANGE\0";
            break;
        case IBV_EVENT_SM_CHANGE:
            str = "IBV_EVENT_SM_CHANGE\0";
            break;
        case IBV_EVENT_SRQ_ERR:
            str = "IBV_EVENT_SRQ_ERR\0";
            break;
        case IBV_EVENT_SRQ_LIMIT_REACHED:
            str = "IBV_EVENT_SRQ_LIMIT_REACHED\0";
            break;
        case IBV_EVENT_QP_LAST_WQE_REACHED:
            str = "IBV_EVENT_QP_LAST_WQE_REACHED\0";
            break;
        case IBV_EVENT_CLIENT_REREGISTER:
            str = "IBV_EVENT_CLIENT_REREGISTER\0";
            break;
        default:
            str = "UNKNOWN_EVENT\0";
    }

    strncpy(viadev_event_code_str, str, 64);

    return viadev_event_code_str;
}
