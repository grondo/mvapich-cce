/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#ifndef MPICH_PWD_H
#define MPICH_PWD_H

#include <winsock2.h>
#include <windows.h>

BOOL SetupCryptoClient();
BOOL SavePasswordToRegistry(TCHAR *szAccount, TCHAR *szPassword, bool persistent=true);
BOOL ReadPasswordFromRegistry(TCHAR *szAccount, TCHAR *szPassword);
BOOL DeleteCurrentPasswordRegistryEntry();

#endif
