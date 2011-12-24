/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   $Id: ad_pvfs_delete.c,v 1.7 2004/10/04 15:51:01 robl Exp $    
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_pvfs.h"
#include "adio.h"

void ADIOI_PVFS_Delete(char *filename, int *error_code)
{
    int err;
    static char myname[] = "ADIOI_PVFS_DELETE";

    err = pvfs_unlink(filename);
    if (err == -1) {
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO,
					   "**io",
					   "**io %s", strerror(errno));
    }
    else *error_code = MPI_SUCCESS;
}
