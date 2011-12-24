/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   $Id: ad_resize.c,v 1.1 2005/09/01 21:31:48 surs Exp $    
 *
 *   Copyright (C) 2004 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "adio.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

void ADIOI_GEN_Resize(ADIO_File fd, ADIO_Offset size, int *error_code)
{
    int err;
    static char myname[] = "ADIOI_GEN_RESIZE";

    err = ftruncate(fd->fd_sys, size);

    /* --BEGIN ERROR HANDLING-- */
    if (err == -1) {
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO,
					   "**io", "**io %s", strerror(errno));
	return;
    }
    /* --END ERROR HANDLING-- */

    *error_code = MPI_SUCCESS;
}
