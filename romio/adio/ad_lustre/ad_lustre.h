/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 *
 *   With contributions from Future Technologies Group, 
 *   Oak Ridge National Laboratory (http://ft.ornl.gov/)
 */

#ifndef AD_UNIX_INCLUDE
#define AD_UNIX_INCLUDE

/* temp*/
#define HAVE_ASM_TYPES_H 1

#include <unistd.h>
#include <linux/types.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "lustre/lustre_user.h"
#include "adio.h"
/*#include "adioi.h"*/

#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif

#ifdef HAVE_AIO_H
#include <aio.h>
#ifdef HAVE_SYS_AIO_H
#include <sys/aio.h>
#endif
#endif /* End of HAVE_SYS_AIO_H */

#endif /* End of AD_UNIX_INCLUDE */
