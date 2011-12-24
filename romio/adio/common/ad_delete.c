/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   $Id: ad_delete.c,v 1.18 2004/10/04 15:51:23 robl Exp $    
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "adio.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

void ADIOI_GEN_Delete(char *filename, int *error_code)
{
    int err;
    static char myname[] = "ADIOI_GEN_DELETE";

    err = unlink(filename);
    if (err == -1) {
	*error_code = MPIO_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
					   myname, __LINE__, MPI_ERR_IO, "**io",
					   "**io %s", strerror(errno));
	return;
    }
    else *error_code = MPI_SUCCESS;
}
