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
 * $Revision: 1.1 $
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <complib/cl_debug.h>
#include <errno.h>
#include "ibmcgrp.h"

#define DEFAULT_RETRY_COUNT 3
#define DEFAULT_TRANS_TIMEOUT_MILLISEC 1000

#define EXIT(x) {complib_exit(); exit(x);}
#define RETURN(x) {complib_exit(); return(x);}


/**********************************************************************
 **********************************************************************/
boolean_t
ibmcgrp_is_debug(void);

boolean_t
ibmcgrp_is_debug()
{
#if defined( _DEBUG_ )
  return TRUE;
#else
  return FALSE;
#endif /* defined( _DEBUG_ ) */
}

/**********************************************************************
 **********************************************************************/
void show_usage(void);

void
show_usage(  )
{
  printf( "\n------- ibmcgrp - Usage and options ----------------------\n" );
  printf( "Usage: one of the following optional flows:\n" );
  printf(" ibmcgrp -c|--create -g|--gid <MGID> -p|--port <GUID> [-r|--rate <RATE>] [-m|--mtu <MTU>]\n" );
  printf(" ibmcgrp -j|--join   -g|--gid <MGID> -p|--port <GUID> \n" );
  printf(" ibmcgrp -l|--leave  -g|--gid <MGID> -p|--port <GUID> \n" );
  printf( "\nOptions:\n" );
  printf( "-g <MGID>\n"
          "--gid <MGID>\n"
          "          This option specifies the multicast gid to be used.\n"
          "          Format is 0xPPPPPPPPPPPPPPPP:GGGGGGGGGGGGGGGG.\n"
          "          A valid MGID is specified by IBTA spec:15.2.5.17.2\n"
          "          E.g. 0xff12a01cfe800000:HHHHHHHHHHHHHHHH (H is any hex)\n"
          "          Where P and G are Hexadecimal digits of Prefix and Guid.\n" );
  printf( "-p <port num>\n"
          "--port_num <port num>\n"
          "          This is the port number used for communicating with\n"
          "          the SA. NOTE it is the port to join/leave the group.\n\n" );
  printf( "-m <LID in hex>\n"
          "--max_lid <LID in hex>\n"
          "          This option specifies the maximal LID number to be searched\n"
          "          for during inventory file build (default to 100).\n");
  printf( "-r <RATE>\n"
          "--rate <RATE>\n"
          "          This option specifies the rate of the multicast\n"
          "          group. The rate codes are:\n"
          "          2 = 2.5GBS\n"
          "          3 = 10GBS (default)\n"
          "          4 = 30GBS\n\n" );
  printf( "-m <MTU>\n"
          "--mtu <MTU>\n"
          "          This option specifies the mtu of the multicast\n"
          "          group. The mtu codes are:\n"
          "          1 = 256 Bytes (default)\n"
          "          2 = 512 Bytes\n"
          "          3 = 1024 Bytes\n"
          "          4 = 2048 Bytes\n"
          "          5 = 4096 Bytes\n\n" );
  printf( "-h\n"
          "--help\n" "          Display this usage info then exit.\n\n" );
  printf( "-o\n"
          "--out_log_file\n"
          "          This option defines the log to be the given file.\n"
          "          By default the log goes to stdout.\n\n");
  printf( "-v\n"
          "          This option increases the log verbosity level.\n"
          "          The -v option may be specified multiple times\n"
          "          to further increase the verbosity level.\n"
          "          See the -vf option for more information about.\n"
          "          log verbosity.\n\n" );
  printf( "-V\n"
          "          This option sets the maximum verbosity level and\n"
          "          forces log flushing.\n"
          "          The -V is equivalent to '-vf 0xFF -d 2'.\n"
          "          See the -vf option for more information about.\n"
          "          log verbosity.\n\n" );
  printf( "-x <flags>\n"
          "          This option sets the log verbosity level.\n"
          "          A flags field must follow the -vf option.\n"
          "          A bit set/clear in the flags enables/disables a\n"
          "          specific log level as follows:\n"
          "          BIT    LOG LEVEL ENABLED\n"
          "          ----   -----------------\n"
          "          0x01 - ERROR (error messages)\n"
          "          0x02 - INFO (basic messages, low volume)\n"
          "          0x04 - VERBOSE (interesting stuff, moderate volume)\n"
          "          0x08 - DEBUG (diagnostic, high volume)\n"
          "          0x10 - FUNCS (function entry/exit, very high volume)\n"
          "          0x20 - FRAMES (dumps all SMP and GMP frames)\n"
          "          0x40 - currently unused.\n"
          "          0x80 - currently unused.\n"
          "          Without -x, ibmcgrp defaults to ERROR + INFO (0x3).\n"
          "          Specifying -x 0 disables all messages.\n"
          "          Specifying -x 0xFF enables all messages (see -V).\n\n" );
}

/**********************************************************************
 **********************************************************************/
/*
  Converts a GID string of the format 0xPPPPPPPPPPPPPPPP:GGGGGGGGGGGGGGGG 
  to a gid type
*/
int
str2gid(
  IN char *str,
  OUT ib_gid_t *p_gid
  );

int
str2gid( 
  IN char *str,
  OUT ib_gid_t *p_gid
  ) 
{
  ib_gid_t temp;
  char buf[38];
  char *p_prefix, *p_guid;

  CL_ASSERT(p_gid);

  strcpy(buf, str);
  p_prefix = buf;
  p_guid = index(buf, ':');
  if (! p_guid)
  {
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
int
main( int argc,
      char *argv[] )
{
  static ibmcgrp_t ibmcgrp;
  /*ibmcgrp_opt_t opt = { 0 }; */
  ibmcgrp_opt_t opt; 
  ib_api_status_t status;
  uint32_t log_flags = OSM_LOG_ERROR | OSM_LOG_INFO;
  uint32_t next_option;
  const char *const short_option = "jlcr:m:p:g:o:vVh";

  /*
   * In the array below, the 2nd parameter specified the number
   * of arguments as follows:
   * 0: no arguments
   * 1: argument
   * 2: optional
   */
  const struct option long_option[] = {
    {"create",    0, NULL, 'c'},
    {"join",      0, NULL, 'j'},
    {"leave",     0, NULL, 'l'},
    {"rate",      1, NULL, 'r'},
    {"mtu",       1, NULL, 'm'},
    {"port_num",  1, NULL, 'p'},
    {"guid",      1, NULL, 'g'},
    {"help",      0, NULL, 'h'},
    {"verbose",   0, NULL, 'v'},
    {"out_log_file",  1, NULL, 'o'},
    {"vf",        1, NULL, 'x'},
    {"V",         0, NULL, 'V'},

    {NULL, 0, NULL, 0}     /* Required at end of array */
  };

  /* Make sure that the opensm, complib and ibmcgrp were compiled using
     same modes (debug/free) */
  if ( osm_is_debug() != cl_is_debug() || osm_is_debug() != ibmcgrp_is_debug() ||
       ibmcgrp_is_debug() != cl_is_debug() )
  {
    fprintf(stderr, "-E- OpenSM, Complib and ibmcgrp were compiled using different modes\n");
    fprintf(stderr, "-E- OpenSM debug:%d Complib debug:%d ibmcgrp debug:%d \n",
            osm_is_debug(), cl_is_debug(), ibmcgrp_is_debug() );
    EXIT(1);
  }

  opt.transaction_timeout = DEFAULT_TRANS_TIMEOUT_MILLISEC;
  opt.retry_count = DEFAULT_RETRY_COUNT;
  opt.force_log_flush = FALSE;
  opt.log_file = NULL;
  opt.action = IBMCGRP_NOACT;

  do
  {
    next_option = getopt_long_only( argc, argv, short_option,
                                    long_option, NULL );
    
    switch ( next_option )
    {
    case 'c':
      /*
       * Create the multicast group
       */
      if (opt.action != IBMCGRP_NOACT)
      {
        printf( "-E- Action already set.\n" );
        EXIT(1);
      }
      opt.action = IBMCGRP_CREATE;
      printf( "-I- Creating Multicast Group\n" );
      break;
       
    case 'j':
      /*
       * Join the multicast group
       */
      if (opt.action != IBMCGRP_NOACT)
      {
        printf( "-E- Action already set.\n" );
        EXIT(1);
      }
      opt.action = IBMCGRP_JOIN;
      printf( "-I- Joining Multicast Group\n" );
      break;

    case 'l':
      /*
       * Leave the multicast group
       */
      if (opt.action != IBMCGRP_NOACT)
      {
        printf( "-E- Action already set.\n" );
        EXIT(1);
      }
      opt.action = IBMCGRP_LEAVE;
      printf( "-I- Leaving Multicast Group\n" );
      break;

    case 'r':
      /*
       * Specifies the created group rate.
       */
      opt.rate = atoi( optarg );
      if ((opt.rate < 2) || (opt.rate > 4))
      {
        printf( "-E- Given rate is out of range.\n" );
        EXIT(1);
      }

      printf( "-I- Rate = %u\n", opt.rate );
      break;

    case 'm':
      /*
       * Specifies the group MTU
       */
      opt.mtu = atoi( optarg );
      if ((opt.mtu < 1) || (opt.mtu > 5))
      {
        printf( "-E- Given mtu is out of range.\n" );
        EXIT(1);
      }

      printf( "-I- Mtu = %u\n", opt.mtu );
      break;

    case 'p':
      /*
       * Specifies port guid with which to bind.
       */
      opt.port_num = atoi( optarg );
      printf( "-I- Port Num:%u\n", opt.port_num );
      break;
      
    case 'g':
      /*
       * Specifies Multicast GID
       */
      if (str2gid( optarg, &opt.mgid ))
      {
        printf( "-E- Given MGID could not be parsed : %s.\n", optarg );
        EXIT(1);
      }
      printf( "-I- MGID 0x%016" PRIx64 ":%016" PRIx64 "\n",
              cl_ntoh64(opt.mgid.unicast.prefix),
              cl_ntoh64(opt.mgid.unicast.interface_id) 
              );
      break;

    case 'o':
      opt.log_file = optarg;
      printf("-I- Log File:%s\n", opt.log_file );
      break;

    case 'v':
      /*
       * Increases log verbosity.
       */
      log_flags = ( log_flags << 1 ) | 1;
      printf( "-I- Verbose option -v (log flags = 0x%X)\n", log_flags );
      break;

    case 'V':
      /*
       * Specifies maximum log verbosity.
       */
      log_flags = 0xFFFFFFFF;
      opt.force_log_flush = TRUE;
      printf( "-I- Enabling maximum log verbosity\n" );
      break;

    case 'h':
      show_usage(  );
      RETURN(0);

    case 'x':
      log_flags = strtol( optarg, NULL, 0 );
      printf( "-I- Verbose option -vf (log flags = 0x%X)\n",
              log_flags );
      break;

    case -1:
      /*      printf( "Done with args\n" ); */
      break;

    default:            /* something wrong */
      abort(  );
    }

  }
  while( next_option != -1 );

  /* Check for mandatory options */
  if (opt.action == IBMCGRP_NOACT)
  {
    printf( "-E- Missing action.\n" );
    EXIT(1);
  }

  if (! opt.port_num ) 
  {
    printf( "-E- Missing port_num.\n" );
    EXIT(1);
  }

  if (! (opt.mgid.unicast.prefix || opt.mgid.unicast.interface_id) ) 
  {
    printf( "-W- Missing MGID: The SM should assign a new MGID.\n" );
  }

  /* init the main object and sub objects (log and osm vendor) */
  status = ibmcgrp_init( &ibmcgrp, &opt, ( osm_log_level_t ) log_flags );
  if( status != IB_SUCCESS )
  {
    printf("-E- fail to init ibmcgrp.\n");
    goto Exit;
  }
  
  /* bind to a specific port */
  status = ibmcgrp_bind( &ibmcgrp );
  if (status != IB_SUCCESS) EXIT(status);

  /* actual work */
  status = ibmcgrp_run( &ibmcgrp );
  if (status != IB_SUCCESS)
  {
    printf("IBMCGRP: FAIL\n");
  }
  else
  {
    printf("IBMCGRP: PASS\n");
  }

  ibmcgrp_destroy( &ibmcgrp );

 Exit:
  RETURN ( status );
}
