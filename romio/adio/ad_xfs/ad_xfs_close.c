/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   $Id: ad_xfs_close.c,v 1.11 2003/04/18 20:15:04 David Exp $    
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_xfs.h"

void ADIOI_XFS_Close(ADIO_File fd, int *error_code)
{
    int err, err1;
#if defined(MPICH2) || !defined(PRINT_ERR_MSG)
    static char myname[] = "ADIOI_XFS_CLOSE";
#endif

    err = close(fd->fd_sys);
    err1 = close(fd->fd_direct);

    if ((err == -1) || (err1 == -1)) {
#ifdef MPICH2
	*error_code = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, myname, __LINE__, MPI_ERR_IO, "**io",
	    "**io %s", strerror(errno));
#elif defined(PRINT_ERR_MSG)
	*error_code = MPI_ERR_UNKNOWN;
#else /* MPICH-1 */
	*error_code = MPIR_Err_setmsg(MPI_ERR_IO, MPIR_ADIO_ERROR,
			      myname, "I/O Error", "%s", strerror(errno));
	ADIOI_Error(fd, *error_code, myname);	    
#endif
    }
    else *error_code = MPI_SUCCESS;
}
