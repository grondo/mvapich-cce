/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_ntfs.h"

void ADIOI_NTFS_Open(ADIO_File fd, int *error_code)
{
    int cmode, amode, smode;
    static char myname[] = "ADIOI_NTFS_OPEN";

    amode = 0;
    cmode = OPEN_EXISTING;
    smode = 0;
    if (fd->access_mode & ADIO_CREATE)
	cmode = OPEN_ALWAYS; /*CREATE_ALWAYS;*/
    if (fd->access_mode & ADIO_EXCL)
	cmode = CREATE_NEW;

    if (fd->access_mode & ADIO_RDONLY)
    {
	amode = amode | FILE_SHARE_READ;
	smode = smode | GENERIC_READ;
    }
    if (fd->access_mode & ADIO_WRONLY)
    {
	amode = amode | FILE_SHARE_WRITE;
	smode = smode | GENERIC_WRITE;
    }
    if (fd->access_mode & ADIO_RDWR)
    {
	amode = amode | FILE_SHARE_READ | FILE_SHARE_WRITE;
	smode = smode | GENERIC_READ | GENERIC_WRITE;
    }

	fd->fd_sys = CreateFile(fd->filename, 
				GENERIC_READ | GENERIC_WRITE,
				amode, 
				NULL, 
				cmode, 
				FILE_ATTRIBUTE_NORMAL, 
				NULL);
        fd->fd_direct = -1;

    if ((fd->fd_sys != INVALID_HANDLE_VALUE) &&
	(fd->access_mode & ADIO_APPEND))
    {
	fd->fp_ind = fd->fp_sys_posn = SetFilePointer(fd->fd_sys, 0, NULL,
						      FILE_END);
    }

    if (fd->fd_sys == INVALID_HANDLE_VALUE) {
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO,
					   "**io",
					   "**io %s", strerror(errno));
	return;
    }
    else *error_code = MPI_SUCCESS;
}

