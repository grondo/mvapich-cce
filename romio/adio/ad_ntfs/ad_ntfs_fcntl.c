/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_ntfs.h"
#include "adio_extern.h"

void ADIOI_NTFS_Fcntl(ADIO_File fd, int flag, ADIO_Fcntl_t *fcntl_struct, int *error_code)
{
    DWORD dwTemp;
    static char myname[] = "ADIOI_NTFS_FCNTL";

    switch(flag) {
    case ADIO_FCNTL_GET_FSIZE:
	fcntl_struct->fsize = SetFilePointer(fd->fd_sys, 0, 0, FILE_END);
	if (fd->fp_sys_posn != -1) 
	{
		dwTemp = DWORDHIGH(fd->fp_sys_posn);
		SetFilePointer(fd->fd_sys, DWORDLOW(fd->fp_sys_posn), &dwTemp, FILE_BEGIN);
	}
	if (fcntl_struct->fsize == -1) {
	    *error_code = MPIO_Err_create_code(MPI_SUCCESS,
					       MPIR_ERR_RECOVERABLE, myname,
					       __LINE__, MPI_ERR_IO, "**io",
					       "**io %s", strerror(errno));
	    return;
	}
	else *error_code = MPI_SUCCESS;
	break;

    case ADIO_FCNTL_SET_DISKSPACE:
	ADIOI_GEN_Prealloc(fd, fcntl_struct->diskspace, error_code);
	break;

    case ADIO_FCNTL_SET_ATOMICITY:
	/* fd->atomicity = (fcntl_struct->atomicity == 0) ? 0 : 1; */
	/* *error_code = MPI_SUCCESS; */
	fd->atomicity = 0;
	*error_code = MPI_ERR_UNSUPPORTED_OPERATION;
	break;

    default:
	/* --BEGIN ERROR HANDLING-- */
	*error_code = MPIO_Err_create_code(MPI_SUCCESS,
					   MPIR_ERR_RECOVERABLE,
					   myname, __LINE__,
					   MPI_ERR_ARG,
					   "**flag", "**flag %d", flag);
	return;
	/* --END ERROR HANDLING-- */
    }
}
