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
 *    Implementation of ibmcgrp_t.
 *
 * Environment:
 *    Linux User Mode
 *
 * $Revision: 1.2 $
 */

#include "ibmcgrp.h"

#define GUID_ARRAY_SIZE 64

/**********************************************************************
 **********************************************************************/
/*
  This function initializes the main object, the log and the Osm Vendor
*/
ib_api_status_t
ibmcgrp_init( IN ibmcgrp_t * const p_ibmcgrp,
              IN ibmcgrp_opt_t * const p_opt,
              IN const osm_log_level_t log_flags
              )
{
  ib_api_status_t status;

  /* just making sure - cleanup the static global obj */
  cl_memclr( p_ibmcgrp, sizeof( *p_ibmcgrp ) );

  /* construct and init the log */
  p_ibmcgrp->p_log = (osm_log_t *)cl_malloc(sizeof(osm_log_t));
  osm_log_construct( p_ibmcgrp->p_log );
  status = osm_log_init( p_ibmcgrp->p_log, p_opt->force_log_flush,
                         0x0001, p_opt->log_file,1 );
  if( status != IB_SUCCESS )
    return ( status );

  osm_log_set_level( p_ibmcgrp->p_log, log_flags );

  /* finaly can declare we are here ... */
  osm_log( p_ibmcgrp->p_log, OSM_LOG_FUNCS,
           "ibmcgrp_init: [\n" );

  /* assign all the opts */
  p_ibmcgrp->p_opt = p_opt;

  /* initialize the osm vendor service object */
  p_ibmcgrp->p_vendor = osm_vendor_new( p_ibmcgrp->p_log,
                                        p_opt->transaction_timeout );

  if( p_ibmcgrp->p_vendor == NULL )
  {
    status = IB_INSUFFICIENT_RESOURCES;
    osm_log( p_ibmcgrp->p_log, OSM_LOG_ERROR,
             "ibmcgrp_init: ERR 0001: "
             "Unable to allocate vendor object" );
    goto Exit;
  }

  /* all mads (actually wrappers) are taken and returned to a pool */
  osm_mad_pool_construct( &p_ibmcgrp->mad_pool );
  status = osm_mad_pool_init(
    &p_ibmcgrp->mad_pool, p_ibmcgrp->p_log );
  if( status != IB_SUCCESS )
    goto Exit;

 Exit:
  osm_log( p_ibmcgrp->p_log, OSM_LOG_FUNCS,
           "ibmcgrp_init: ]\n" );
  return ( status );
}

/**********************************************************************
 **********************************************************************/
ib_api_status_t
ibmcgrp_bind( IN ibmcgrp_t * p_ibmcgrp )
{
  ib_api_status_t status;
  uint32_t num_ports = GUID_ARRAY_SIZE;
  ib_port_attr_t attr_array[GUID_ARRAY_SIZE];

  OSM_LOG_ENTER( p_ibmcgrp->p_log, ibmcgrp_bind );

  /*
   * Call the transport layer for a list of local port
   * GUID values.
   */
  status = osm_vendor_get_all_port_attr( p_ibmcgrp->p_vendor,
                                         attr_array, &num_ports );
  if ( status != IB_SUCCESS )
  {
    osm_log( p_ibmcgrp->p_log, OSM_LOG_ERROR,
             "ibmcgrp_bind: ERR 00134: "
             "Failure getting local port attributes (%s)\n",
             ib_get_err_str( status ) );
    goto Exit;
  }

  /* make sure the requested port exists */
  if ( p_ibmcgrp->p_opt->port_num > num_ports )
  {
    osm_log( p_ibmcgrp->p_log, OSM_LOG_ERROR,
             "ibmcgrp_bind: ERR 00136: "
             "Given port number out of range %u > %u\n",
             p_ibmcgrp->p_opt->port_num , num_ports );
    status = IB_NOT_FOUND;
    goto Exit;
  }

  /* check if the port is active */
  if ((attr_array[p_ibmcgrp->p_opt->port_num - 1].link_state > IB_LINK_NO_CHANGE) &&
      (attr_array[p_ibmcgrp->p_opt->port_num - 1].link_state < IB_LINK_ACTIVE))
  {
    osm_log( p_ibmcgrp->p_log, OSM_LOG_ERROR,
             "ibmcgrp_bind: ERR 00136: "
             "Given port number link state is not active: %s.\n",
             ib_get_port_state_str(
               attr_array[p_ibmcgrp->p_opt->port_num - 1].link_state )
             );
    status = IB_NOT_FOUND;
    goto Exit;
  }

  p_ibmcgrp->port_guid = attr_array[p_ibmcgrp->p_opt->port_num - 1].port_guid;

  /* ok finally bind the sa interface to this port */
  p_ibmcgrp->h_bind =
    osmv_bind_sa(p_ibmcgrp->p_vendor,
                 &p_ibmcgrp->mad_pool,
                 p_ibmcgrp->port_guid);

  if(  p_ibmcgrp->h_bind == OSM_BIND_INVALID_HANDLE )
  {
    osm_log( p_ibmcgrp->p_log, OSM_LOG_ERROR,
             "ibmcgrp_bind: ERR 00137: "     
             "Unable to bind to SA\n" );
    status = IB_ERROR;
    goto Exit;
  }

 Exit:
  OSM_LOG_EXIT( p_ibmcgrp->p_log );
  return ( status );
}

/**********************************************************************
 **********************************************************************/
void
ibmcgrp_destroy( IN ibmcgrp_t * p_ibmcgrp )
{
  if( p_ibmcgrp->p_vendor )
  {
    osm_vendor_delete( &p_ibmcgrp->p_vendor );
  }

  osm_log_destroy( p_ibmcgrp->p_log );
  cl_free( p_ibmcgrp->p_log );
}

/**********************************************************************
 **********************************************************************/

/****s* Multicast_Group_App/ibmcgrp_req_context_t
 * NAME
 * ibmcgrp_req_context_t
 *
 * DESCRIPTION
 * Query context for ib_query callback function.
 *
 * SYNOPSIS
 */
typedef struct _ibmcgrp_req_context
{
  ibmcgrp_t *p_ibmcgrp;
  osmv_query_res_t result;

}
ibmcgrp_req_context_t;
/*
 * FIELDS
 *
 * SEE ALSO
 *********/

/**********************************************************************
 **********************************************************************/
/*
   This function is called by the osmv_query_sa when the result is
   obtained. We need to provdie it even that we use a synchronoues flow.
*/
void
ibmcgrp_query_res_cb( IN osmv_query_res_t *  );

void
ibmcgrp_query_res_cb( IN osmv_query_res_t * p_rec )
{
  ibmcgrp_req_context_t *const p_ctxt =
    ( ibmcgrp_req_context_t * ) p_rec->query_context;
  ibmcgrp_t *const p_ibmcgrp = p_ctxt->p_ibmcgrp;

  OSM_LOG_ENTER( p_ibmcgrp->p_log, ibmcgrp_query_res_cb );

  p_ctxt->result = *p_rec;

  if( p_rec->status != IB_SUCCESS )
  {
    if ( p_rec->status != IB_INVALID_PARAMETER )
    {
      osm_log( p_ibmcgrp->p_log, OSM_LOG_ERROR,
               "ibmcgrp_query_res_cb: ERR 0003: "
               "Error on query (%s).\n", ib_get_err_str( p_rec->status ) );
    }
  }

  OSM_LOG_EXIT( p_ibmcgrp->p_log );
}

/**********************************************************************
 **********************************************************************/

ib_api_status_t
ibmcgrp_run( IN ibmcgrp_t * p_ibmcgrp )
{
  ibmcgrp_req_context_t context;
  ib_api_status_t       status = IB_SUCCESS;
  osmv_user_query_t     user;
  osmv_query_req_t      req;
  ib_member_rec_t       mc_rec, *p_mcmr;
  ib_mad_t              *p_res;
  char                  *p_action_str = NULL;

  OSM_LOG_ENTER( p_ibmcgrp->p_log, ibmcgrp_run );

  /*
   * Do a blocking query for this record in the subnet.
   *
   * The query structures are locals.
   */
  cl_memclr( &req, sizeof( req ) );
  cl_memclr( &user, sizeof( user ) );
  cl_memclr( &context, sizeof( context ) );

  /* initialize some defaults on the MC grp request */

  /* use default values so we can change only what we want later */
  cl_memclr(&mc_rec,sizeof(ib_member_rec_t));

  /* Use the MGID provided */
  cl_memcpy(&mc_rec.mgid, &(p_ibmcgrp->p_opt->mgid), sizeof(ib_gid_t));

  /* our own port gid - as stored in the main object  */
  cl_memcpy(&mc_rec.port_gid.unicast.interface_id ,
            &p_ibmcgrp->port_guid,
            sizeof(p_ibmcgrp->port_guid)
            );

  /* we assume default subnet prefix */
  mc_rec.port_gid.unicast.prefix = CL_HTON64(0xFE80000000000000ULL);

  /* we want to use a link local scope: 0x02 */
  mc_rec.scope_state = ib_member_set_scope_state(0x02, 0);

  /* we always use full membership - for now */
  ib_member_set_join_state(&mc_rec, IB_MC_REC_STATE_FULL_MEMBER);

  /*
     Following MC request fields empty:
    
     ib_net32_t            qkey;
     ib_net16_t            mlid; - we keep it zero for upper level to decide.
     uint8_t               tclass;  (between subnets clasification)
     ib_net16_t            pkey; leave as zero
     uint8_t               pkt_life; zero means greater than zero ...
     ib_net32_t            sl_flow_hop; keep it all zeros
  */
 
  switch (p_ibmcgrp->p_opt->action)
  {
  case IBMCGRP_CREATE:
    req.query_type = OSMV_QUERY_UD_MULTICAST_SET;
    mc_rec.rate = p_ibmcgrp->p_opt->rate;
    mc_rec.mtu = p_ibmcgrp->p_opt->mtu;
    user.comp_mask =
      IB_MCR_COMPMASK_MGID |
      IB_MCR_COMPMASK_PORT_GID |
      IB_MCR_COMPMASK_QKEY |
      IB_MCR_COMPMASK_PKEY |
      IB_MCR_COMPMASK_FLOW |
      IB_MCR_COMPMASK_JOIN_STATE |
      IB_MCR_COMPMASK_SL |
      IB_MCR_COMPMASK_TCLASS |  // all above are required
      IB_MCR_COMPMASK_RATE_SEL |
      IB_MCR_COMPMASK_RATE |
      IB_MCR_COMPMASK_MTU_SEL |
      IB_MCR_COMPMASK_MTU;
    p_action_str = "Created";
    break;
  case IBMCGRP_JOIN:
    req.query_type = OSMV_QUERY_UD_MULTICAST_SET;
    user.comp_mask =
      IB_MCR_COMPMASK_MGID |
      IB_MCR_COMPMASK_PORT_GID |
      IB_MCR_COMPMASK_JOIN_STATE;
    p_action_str = "Joined";
    break;
  case IBMCGRP_LEAVE:
    req.query_type = OSMV_QUERY_UD_MULTICAST_DELETE;
    user.comp_mask =
      IB_MCR_COMPMASK_MGID |
      IB_MCR_COMPMASK_PORT_GID |
      IB_MCR_COMPMASK_JOIN_STATE;
    p_action_str = "Left";
    break;
  }

  /* we provide the attribute to the request */
  user.p_attr = &mc_rec;

  /* fields controlling the sa request */
  req.timeout_ms = p_ibmcgrp->p_opt->transaction_timeout;
  req.retry_cnt = p_ibmcgrp->p_opt->retry_count;
  req.flags = OSM_SA_FLAGS_SYNC;
  req.query_context = &context;
  req.pfn_query_cb = ibmcgrp_query_res_cb;
  req.p_query_input = &user;
  req.sm_key = 0;

  /* to ease the callback we provide it the top object */
  context.p_ibmcgrp = p_ibmcgrp;

  /* perform the request */
  status = osmv_query_sa( p_ibmcgrp->h_bind, &req );
 
  /* examine the status of the send */
  if ( status != IB_SUCCESS )
  {
    osm_log( p_ibmcgrp->p_log, OSM_LOG_ERROR,
             "ibmcgrp_run: ERR 0205: "
             "ib_query failed (%s).\n", ib_get_err_str( status ) );
    goto Exit;
  }

  /* now examine the remote status as returned in the mad */
  status = context.result.status;
  p_res = osm_madw_get_mad_ptr ( context.result.p_result_madw );
  if( status != IB_SUCCESS )
  {
    if( status == IB_REMOTE_ERROR )
    {
      osm_log( p_ibmcgrp->p_log, OSM_LOG_ERROR,
               "ibmcgrp_run: "
               "Remote error = 0x%04X.\n",
               cl_ntoh16(p_res->status)
               );
    }
  }
  else
  {
    /* get the returned MAD contents and pront it out */
    p_mcmr = osmv_get_query_result( context.result.p_result_madw, 0 );
    printf("-I- %s the Multicast Group:\n"
           "\tMGID....................0x%016" PRIx64 " : "
           "0x%016" PRIx64 "\n"
           "\tPortGid.................0x%016" PRIx64 " : "
           "0x%016" PRIx64 "\n"
           "\tqkey....................0x%X\n"
           "\tMlid....................0x%X\n"
           "\tScopeState..............0x%X\n"
           "\tRate....................0x%X\n"
           "\tMtu.....................0x%X\n"
           "",
           p_action_str,
           cl_ntoh64( p_mcmr->mgid.unicast.prefix ),
           cl_ntoh64( p_mcmr->mgid.unicast.interface_id ),
           cl_ntoh64( p_mcmr->port_gid.unicast.prefix ),
           cl_ntoh64( p_mcmr->port_gid.unicast.interface_id ),
           cl_ntoh32( p_mcmr->qkey ),
           cl_ntoh16( p_mcmr->mlid ),
           p_mcmr->scope_state,
           p_mcmr->rate,
           p_mcmr->mtu
           );
  }

 Exit:
  /*
   * Return the IB query MAD to the pool as necessary.
   */
  if( context.result.p_result_madw != NULL )
  {
    osm_mad_pool_put( &p_ibmcgrp->mad_pool, context.result.p_result_madw );
    context.result.p_result_madw = NULL;
  }

  OSM_LOG_EXIT( p_ibmcgrp->p_log );
  return ( status );
}
