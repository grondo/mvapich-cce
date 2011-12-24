/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   $Id: ad_ntfs_flush.c,v 1.9 2004/10/04 15:50:54 robl Exp $    
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_ntfs.h"

void ADIOI_NTFS_Flush(ADIO_File fd, int *error_code)
{
    int err;
    static char myname[] = "ADIOI_GEN_FLUSH";

    err = (fd->access_mode & ADIO_RDONLY) ? TRUE :
	FlushFileBuffers(fd->fd_sys);

    if (err == FALSE) {
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO,
					   "**io",
					   "**io %s", strerror(errno));
	return;
    }
    else *error_code = MPI_SUCCESS;
}
