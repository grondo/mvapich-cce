/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* 
 *   $Id: ad_ntfs_rdcoll.c,v 1.2 2002/10/24 17:00:49 gropp Exp $    
 *
 *   Copyright (C) 1997 University of Chicago. 
 *   See COPYRIGHT notice in top-level directory.
 */

#include "ad_ntfs.h"

void ADIOI_NTFS_ReadStridedColl(ADIO_File fd, void *buf, int count,
                       MPI_Datatype datatype, int file_ptr_type,
                       ADIO_Offset offset, ADIO_Status *status, int
                       *error_code)
{
    ADIOI_GEN_ReadStridedColl(fd, buf, count, datatype, file_ptr_type,
			      offset, status, error_code);
}
