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
 *
 * psmconfig.h
 * 
 * We don't really need to do a barrier, but it may help debugging
 * and catch bad codes 
 */
#define MPID_END_NEEDS_BARRIER

#define MPID_HAS_PROC_INFO 

/* This is needed if you want TotalView to acquire all of the processes
 * automagically.
 * If it is defined you must also define 
 *      int MPID_getpid(int index, char **hostname, char **imagename);
 * which takes an index in COMM_WORLD and returns the pid of that process 
 * as a result, and also fills in the pointers to 
 * the two strings hostname (something which we can
 * pass to inet_addr, and image_name which is the name of the executable running
 * that this process is running.
 * You can fill in either (or both) pointers as (char *)0 which means
 * "the same as the master process".
 */

/* Put macro-definitions of routines here */
int vapi_proc_info ( int, char **, char ** );
#define MPID_getpid(i,n,e) vapi_proc_info((i),(n),(e))

