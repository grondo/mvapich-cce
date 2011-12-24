/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   $Id: ad_ntfs_resize.c,v 1.8 2004/10/04 15:50:55 robl Exp $    
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_ntfs.h"

void ADIOI_NTFS_Resize(ADIO_File fd, ADIO_Offset size, int *error_code)
{
    DWORD dwTemp;
    int err;
    static char myname[] = "ADIOI_NTFS_RESIZE";

    dwTemp = DWORDHIGH(size);
    err = SetFilePointer(fd->fd_sys, DWORDLOW(size), &dwTemp, FILE_BEGIN);
    err = SetEndOfFile(fd->fd_sys);
    if (err == FALSE) {
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO,
					   "**io",
					   "**io %s", strerror(errno));
	return;
    }
    else *error_code = MPI_SUCCESS;
}
