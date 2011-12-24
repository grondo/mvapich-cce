/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   $Id: ad_pvfs2_rdcoll.c,v 1.1 2003/06/24 22:43:45 robl Exp $
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_pvfs2.h"

/* not referenced from operations table */

void ADIOI_PVFS2_ReadStridedColl(ADIO_File fd, void *buf, int count,
                       MPI_Datatype datatype, int file_ptr_type,
                       ADIO_Offset offset, ADIO_Status *status, int
                       *error_code)
{
    ADIOI_GEN_ReadStridedColl(fd, buf, count, datatype, file_ptr_type,
			      offset, status, error_code);
}
