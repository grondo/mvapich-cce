/*
 *
 * Copyright (C) 1993 University of Chicago
 * Copyright (C) 1993 Mississippi State University
 *
 * Use of this software is under license. The license agreement is included
 * in the file MVICH_LICENSE.TXT.
 *
 * Developed at Argonne National Laboratory and Mississippi State University as part of MPICH.
 *
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


#ifndef MPIR_COOKIE
/*****************************************************************************
*  We place "cookies" into the data structures to improve error detection and*
*  reporting of invalid objects.  In order to make this flexible, the        *
*  cookies are defined as macros.                                            *
*  If MPIR_HAS_COOKIES is not defined, then the "cookie" fields are not      *
*  set or tested                                                             *
*  clr cookie increments the cookie value by one, allowing an object to      *
*  still be identified after it has been freed                               *
*****************************************************************************/

#ifdef MPIR_HAS_COOKIES
#define MPIR_COOKIE unsigned long cookie;
#define MPIR_SET_COOKIE(obj,value) (obj)->cookie = (value);
#define MPIR_CLR_COOKIE(obj)       (obj)->cookie ++;
#else
#define MPIR_COOKIE unsigned long cookie; /* Much more safe is define MPIR_COOKIE !!! */
#define MPIR_SET_COOKIE(obj,value)
#define MPIR_CLR_COOKIE(obj)
#endif
/****************************************************************************/

#endif
