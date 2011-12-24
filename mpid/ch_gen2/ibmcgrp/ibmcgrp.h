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
 *    Declaration of ibmcgrp_t.
 * This object represents the ibmcgrp object.
 *
 * Environment:
 *    Linux User Mode
 *
 * $Revision: 1.2 $
 */

#ifndef _IBMCGRP_H_
#define _IBMCGRP_H_

#include <complib/cl_qmap.h>
#include <opensm/osm_log.h>
#include <vendor/osm_vendor_api.h>
#include <vendor/osm_vendor_sa_api.h>
#include <opensm/osm_mad_pool.h>

/****h* Multicast_Group_App/Ibmcgrp
 * NAME
 * Ibmcgrp
 *
 * DESCRIPTION
 * The Ibmcgrp object create/join and leave multicast group.
 *
 * AUTHOR
 * Eitan Zahavi, Mellanox.
 *
 *********/

/****d* Multicast_Group_App/ibmcgrp_action
 * NAME
 *  ibmcgrp_action
 *
 * DESCRIPTION
 *  Enumerates the possible Action modes.
 *
 * SYNOPSYS
 */
#define IBMCGRP_NOACT  0
#define IBMCGRP_CREATE 1
#define IBMCGRP_JOIN   2
#define IBMCGRP_LEAVE  3
/********/

/****s* Multicast_Group_App/ibmcgrp_opt_t
 * NAME
 * ibmcgrp_opt_t
 *
 * DESCRIPTION
 * Ibmcgrp options structure.  This structure contains the various
 * specific configuration parameters for ibmcgrp.
 *
 * SYNOPSYS
 */
typedef struct _ibmcgrp_opt
{
  ib_gid_t  mgid;
  uint16_t  port_num;
  uint16_t  rate;
  uint16_t  mtu;
  uint32_t  transaction_timeout;
  boolean_t force_log_flush;
  uint32_t retry_count;
  uint8_t  action;
  char *log_file;
  boolean_t accum_log_file;
} ibmcgrp_opt_t;
/*
 * FIELDS
 *
 * SEE ALSO
 *********/

/****s* Multicast_Group_App/ibmcgrp_t
 * NAME
 * ibmcgrp_t
 *
 * DESCRIPTION
 * Ibmcgrp structure.
 *
 * This object should be treated as opaque and should
 * be manipulated only through the provided functions.
 *
 * SYNOPSYS
 */
typedef struct _ibmcgrp
{
  osm_log_t          *p_log;
  struct _osm_vendor *p_vendor;
  osm_bind_handle_t   h_bind;
  osm_mad_pool_t      mad_pool;

  ibmcgrp_opt_t       *p_opt;
  ib_net64_t          port_guid;
} ibmcgrp_t;
/*
 * FIELDS
 * p_log
 *    Log facility used by all Ibmcgrp components.
 *
 * p_vendor
 *    Pointer to the vendor transport layer.
 *
 *  h_bind
 *     The bind handle obtained by osm_vendor_sa_api/osmv_bind_sa
 *
 *  mad_pool
 *     The mad pool provided for teh vendor layer to allocate mad wrappers in
 *
 * p_opt
 *    ibmcgrp options structure
 *
 * guid
 *    guid for the port over which ibmcgrp is running.
 *
 * SEE ALSO
 *********/

/****f* Multicast_Group_App/ibmcgrp_destroy
 * NAME
 * ibmcgrp_destroy
 *
 * DESCRIPTION
 * The ibmcgrp_destroy function destroys an ibmcgrp object, releasing
 * all resources.
 *
 * SYNOPSIS
 */
void ibmcgrp_destroy( IN ibmcgrp_t * p_ibmcgrp );

/*
 * PARAMETERS
 * p_ibmcgrp
 *    [in] Pointer to a Multicast_Group_App object to destroy.
 *
 * RETURN VALUE
 * This function does not return a value.
 *
 * NOTES
 * Performs any necessary cleanup of the specified Multicast_Group_App object.
 * Further operations should not be attempted on the destroyed object.
 * This function should only be called after a call to ibmcgrp_init.
 *
 * SEE ALSO
 * ibmcgrp_init
 *********/

/****f* Multicast_Group_App/ibmcgrp_init
 * NAME
 * ibmcgrp_init
 *
 * DESCRIPTION
 * The ibmcgrp_init function initializes a Multicast_Group_App object for use.
 *
 * SYNOPSIS
 */
ib_api_status_t ibmcgrp_init( IN ibmcgrp_t * const p_ibmcgrp,
                              IN ibmcgrp_opt_t * const p_opt,
                              IN const osm_log_level_t log_flags
                              );

/*
 * PARAMETERS
 * p_ibmcgrp
 *    [in] Pointer to an ibmcgrp_t object to initialize.
 *
 * p_opt
 *    [in] Pointer to the options structure.
 *
 * log_flags
 *    [in] Log level flags to set.
 *
 * RETURN VALUES
 * IB_SUCCESS if the Multicast_Group_App object was initialized successfully.
 *
 * NOTES
 * Allows calling other Multicast_Group_App methods.
 *
 * SEE ALSO
 * ibmcgrp object, ibmcgrp_construct, ibmcgrp_destroy
 *********/


/****f* Multicast_Group_App/ibmcgrp_bind
 * NAME
 * ibmcgrp_bind
 *
 * DESCRIPTION
 * Binds ibmcgrp to a local port.
 *
 * SYNOPSIS
 */
ib_api_status_t ibmcgrp_bind( IN ibmcgrp_t * p_ibmcgrp );
/*
 * PARAMETERS
 * p_ibmcgrp
 *    [in] Pointer to an ibmcgrp_t object.
 *
 * RETURN VALUES
 * IB_SUCCESS if OK
 *
 * NOTES
 *
 * SEE ALSO
 *********/

/****f* Multicast_Group_App/ibmcgrp_run
 * NAME
 * ibmcgrp_run
 *
 * DESCRIPTION
 * Runs the ibmcgrp action: create, join, leave
 *
 * SYNOPSIS
 */
ib_api_status_t ibmcgrp_run( IN ibmcgrp_t * const p_ibmcgrp );

/*
 * PARAMETERS
 * p_ibmcgrp
 *    [in] Pointer to an ibmcgrp_t object.
 *
 * RETURN VALUES
 * IB_SUCCESS on success
 *
 * NOTES
 *
 * SEE ALSO
 *********/

#endif /* */
