/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   $Id: ad_piofs_close.c,v 1.9 2003/04/18 20:14:59 David Exp $    
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_piofs.h"
#ifdef PROFILE
#include "mpe.h"
#endif

void ADIOI_PIOFS_Close(ADIO_File fd, int *error_code)
{
    int err;
#ifndef PRINT_ERR_MSG
    static char myname[] = "ADIOI_PIOFS_CLOSE";
#endif

#ifdef PROFILE
    MPE_Log_event(9, 0, "start close");
#endif
    err = close(fd->fd_sys);
#ifdef PROFILE
    MPE_Log_event(10, 0, "end close");
#endif
    if (err == -1) {
#ifdef MPICH2
	*error_code = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, myname, __LINE__, MPI_ERR_IO, "**io",
	    "**io %s", strerror(errno));
#elif defined(PRINT_ERR_MSG)
	*error_code =  MPI_ERR_UNKNOWN;
#else /* MPICH-1 */
	*error_code = MPIR_Err_setmsg(MPI_ERR_IO, MPIR_ADIO_ERROR,
			      myname, "I/O Error", "%s", strerror(errno));
	ADIOI_Error(fd, *error_code, myname);	    
#endif
    }
    else *error_code = MPI_SUCCESS;
}
