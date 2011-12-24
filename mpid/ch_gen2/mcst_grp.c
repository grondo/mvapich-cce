/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  This software program is available to you under a choice of one of two
  licenses.  You may choose to be licensed under either the GNU General Public
  License (GPL) Version 2, June 1991, available at
  http://www.fsf.org/copyleft/gpl.html, or the Intel BSD + Patent License,
  the text of which follows:

  "Recipient" has requested a license and Intel Corporation ("Intel")
  is willing to grant a license for the software entitled
  InfiniBand(tm) System Software (the "Software") being provided by
  Intel Corporation.

  The following definitions apply to this License:

  "Licensed Patents" means patent claims licensable by Intel Corporation which
  are necessarily infringed by the use or sale of the Software alone or when
  combined with the operating system referred to below.

  "Recipient" means the party to whom Intel delivers this Software.
  "Licensee" means Recipient and those third parties that receive a license to
  any operating system available under the GNU Public License version 2.0 or
  later.

  Copyright (c) 1996-2003 Intel Corporation. All rights reserved.

  The license is provided to Recipient and Recipient's Licensees under the
  following terms.

  Redistribution and use in source and binary forms of the Software, with or
  without modification, are permitted provided that the following
  conditions are met:
  Redistributions of source code of the Software may retain the above copyright
  notice, this list of conditions and the following disclaimer.

  Redistributions in binary form of the Software may reproduce the above
  copyright notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.

  Neither the name of Intel Corporation nor the names of its contributors shall
  be used to endorse or promote products derived from this Software without
  specific prior written permission.

  Intel hereby grants Recipient and Licensees a non-exclusive, worldwide,
  royalty-free patent license under Licensed Patents to make, use, sell, offer
  to sell, import and otherwise transfer the Software, if any, in source code
  and object code form. This license shall include changes to the Software that
  are error corrections or other minor changes to the Software that do not add
  functionality or features when the Software is incorporated in any version of
  a operating system that has been distributed under the GNU General Public
  License 2.0 or later.  This patent license shall apply to the combination of
  the Software and any operating system licensed under the GNU Public License
  version 2.0 or later if, at the time Intel provides the Software to
  Recipient, such addition of the Software to the then publicly
  available versions of such operating system available under the GNU
  Public License version 2.0 or later (whether in gold, beta or alpha
  form) causes such combination to be covered by the Licensed
  Patents. The patent license shall not apply to any other
  combinations which include the Software. No hardware per se is
  licensed hereunder.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL INTEL OR ITS CONTRIBUTORS
  BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
  OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
  OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.
  --------------------------------------------------------------------------*/


/*
 * Abstract:
 *    Command line interface for ibmcgrp.
 *    Parse and fill in the options and call the actual code.
 *    Implemented in ibmcgrp:
 *     Initialize the ibmgrp object (and log) 
 *     Bind ibmgrp to the requested IB port.
 *     Run the actual command
 *
 * Environment:
 *    Linux User Mode
 *
 * $Revision: 1.2 $
 */
#if defined(MCST_SUPPORT) 

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <complib/cl_debug.h>
#include <errno.h>
#include "ibmcgrp.h"
#include "viutil.h"

#define DEFAULT_RETRY_COUNT_MCST 3
#define DEFAULT_TRANS_TIMEOUT_MILLISEC 1000

#define EXIT(x) {complib_exit(); exit(x);}
#define RETURN(x) {complib_exit(); return(x);}


void leave_mcgrp(int);
void create_mcgrp(int);
int join_mcgrp(int);
int mcg_lid;

/**********************************************************************
 **********************************************************************/
/*
  Converts a GID string of the format 0xPPPPPPPPPPPPPPPP:GGGGGGGGGGGGGGGG 
  to a gid type
*/
int str2gid(IN char *str, OUT ib_gid_t * p_gid);

int str2gid(IN char *str, OUT ib_gid_t * p_gid)
{
    ib_gid_t temp;
    char buf[38];
    char *p_prefix, *p_guid;

    CL_ASSERT(p_gid);

    strcpy(buf, str);
    p_prefix = buf;
    p_guid = index(buf, ':');
    if (!p_guid) {
        printf("Wrong format for gid %s\n", buf);
        return 1;
    }

    *p_guid = '\0';
    p_guid++;

    errno = 0;
    temp.unicast.prefix = cl_hton64(strtoull(p_prefix, NULL, 0));
    if (errno) {
        printf("Wrong format for gid prefix:%s (got %u)\n",
               p_prefix, errno);
        return 1;
    }

    temp.unicast.interface_id = cl_hton64(strtoull(p_guid, NULL, 16));
    if (errno) {
        printf("Wrong format for gid guid:%s\n", p_guid);
        return 1;
    }

    *p_gid = temp;
    return 0;
}

/**********************************************************************
 **********************************************************************/
void create_mcgrp(int active_port)
{
    static ibmcgrp_t ibmcgrp;
    /*ibmcgrp_opt_t opt = { 0 }; */
    ibmcgrp_opt_t opt;
    ib_api_status_t status;
    uint32_t log_flags = OSM_LOG_ERROR | OSM_LOG_INFO;

    opt.transaction_timeout = DEFAULT_TRANS_TIMEOUT_MILLISEC;
    opt.retry_count = DEFAULT_RETRY_COUNT_MCST;
    opt.force_log_flush = FALSE;
    opt.log_file = NULL ; 
    opt.action = IBMCGRP_NOACT;

    opt.action = IBMCGRP_CREATE;
    opt.port_num = active_port;
    str2gid("0xff13a01cfe800000:0000000000000000", &(opt.mgid));


    /* Check for mandatory options */
    if (opt.action == IBMCGRP_NOACT) {
        printf("-E- Missing action.\n");
        EXIT(1);
    }

    if (!opt.port_num) {
        printf("-E- Missing port_num.\n");
        EXIT(1);
    }

    if (!(opt.mgid.unicast.prefix || opt.mgid.unicast.interface_id)) {
        printf("-W- Missing MGID: The SM should assign a new MGID.\n");
    }

    /* init the main object and sub objects (log and osm vendor) */
    status = ibmcgrp_init(&ibmcgrp, &opt, (osm_log_level_t) log_flags);
    if (status != IB_SUCCESS) {
        printf("-E- fail to init ibmcgrp.\n");
        RETURN ( status );
    }

    /* bind to a specific port */
    status = ibmcgrp_bind(&ibmcgrp);
    if (status != IB_SUCCESS)
        EXIT(status);

    /* actual work */
    status = ibmcgrp_run(&ibmcgrp);
    if (status != IB_SUCCESS) {
        printf("ibmcgrp create failed\n");
    } else {
        co_print("IBMCGRP: PASS\n");
    }

    ibmcgrp_destroy(&ibmcgrp);

}

/**********************************************************************
 **********************************************************************/
int join_mcgrp(int active_port)
{
    static ibmcgrp_t ibmcgrp;
    /*ibmcgrp_opt_t opt = { 0 }; */
    ibmcgrp_opt_t opt;
    ib_api_status_t status;
    uint32_t log_flags = OSM_LOG_ERROR | OSM_LOG_INFO;

    opt.transaction_timeout = DEFAULT_TRANS_TIMEOUT_MILLISEC;
    opt.retry_count = DEFAULT_RETRY_COUNT_MCST;
    opt.force_log_flush = FALSE;
    opt.log_file = NULL; 

    opt.action = IBMCGRP_JOIN;
    opt.port_num = active_port;
    str2gid("0xff13a01cfe800000:0000000000000000", &(opt.mgid));


    /* Check for mandatory options */
    if (opt.action == IBMCGRP_NOACT) {
        printf("-E- Missing action.\n");
        EXIT(1);
    }

    if (!opt.port_num) {
        printf("-E- Missing port_num.\n");
        EXIT(1);
    }

    if (!(opt.mgid.unicast.prefix || opt.mgid.unicast.interface_id)) {
        printf("-W- Missing MGID: The SM should assign a new MGID.\n");
    }

    /* init the main object and sub objects (log and osm vendor) */
    status = ibmcgrp_init(&ibmcgrp, &opt, (osm_log_level_t) log_flags);
    if (status != IB_SUCCESS) {
        printf("-E- fail to init ibmcgrp.\n");
        RETURN(status);
    }

    /* bind to a specific port */
    status = ibmcgrp_bind(&ibmcgrp);
    if (status != IB_SUCCESS)
        EXIT(status);

    /* actual work */
    status = ibmcgrp_run(&ibmcgrp);
    if (status != IB_SUCCESS) {
        printf("ibmcgrp join failed\n");
    } else {
        co_print("IBMCGRP: PASS\n");
    }

    ibmcgrp_destroy(&ibmcgrp);
    return mcg_lid;

}

/**********************************************************************
 **********************************************************************/
void leave_mcgrp(int active_port)
{
    static ibmcgrp_t ibmcgrp;
    /*ibmcgrp_opt_t opt = { 0 }; */
    ibmcgrp_opt_t opt;
    ib_api_status_t status;
    uint32_t log_flags = OSM_LOG_ERROR | OSM_LOG_INFO;

    opt.transaction_timeout = DEFAULT_TRANS_TIMEOUT_MILLISEC;
    opt.retry_count = DEFAULT_RETRY_COUNT_MCST;
    opt.force_log_flush = FALSE;
    opt.log_file = NULL; 
    opt.action = IBMCGRP_NOACT;

    opt.action = IBMCGRP_LEAVE;
    opt.port_num = active_port;
    str2gid("0xff13a01cfe800000:0000000000000000", &(opt.mgid));


    /* Check for mandatory options */
    if (opt.action == IBMCGRP_NOACT) {
        printf("-E- Missing action.\n");
        EXIT(1);
    }

    if (!opt.port_num) {
        printf("-E- Missing port_num.\n");
        EXIT(1);
    }

    if (!(opt.mgid.unicast.prefix || opt.mgid.unicast.interface_id)) {
        printf("-W- Missing MGID: The SM should assign a new MGID.\n");
    }

    /* init the main object and sub objects (log and osm vendor) */
    status = ibmcgrp_init(&ibmcgrp, &opt, (osm_log_level_t) log_flags);
    if (status != IB_SUCCESS) {
        printf("-E- fail to init ibmcgrp.\n");
        RETURN(status);
    }

    /* bind to a specific port */
    status = ibmcgrp_bind(&ibmcgrp);
    if (status != IB_SUCCESS)
        EXIT(status);

    /* actual work */
    status = ibmcgrp_run(&ibmcgrp);
    if (status != IB_SUCCESS) {
        printf("ibmcgrp leave failed\n");
    } else {
        co_print("IBMCGRP: PASS\n");
    }

    ibmcgrp_destroy(&ibmcgrp);

}

#else
#endif
