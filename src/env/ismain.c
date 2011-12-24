#ifdef HAVE_WEAK_SYMBOLS

/*
 * Undefing MPI_Is_thread_main if mpi.h defined it to help catch
 * library/headerfile conflicts
 */
#ifdef MPI_Is_thread_main
#   undef MPI_Is_thread_main
#   undef PMPI_Is_thread_main
#endif

#if defined(HAVE_PRAGMA_WEAK)
#   pragma weak MPI_Is_thread_main = PMPI_Is_thread_main
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#   pragma _HP_SECONDARY_DEF PMPI_Is_thread_main  MPI_Is_thread_main
#elif defined(HAVE_PRAGMA_CRI_DUP)
#   pragma _CRI duplicate MPI_Is_thread_main as PMPI_Is_thread_main
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif /* HAVE_WEAK_SYMBOLS */

/*@
   MPI_Is_thread_main - Returns a flag indicating whether this thread called 
                        'MPI_Init' or 'MPI_Init_thread'

   Output Parameter:
. flag - Flag is true if 'MPI_Init' or 'MPI_Init_thread' has been called by 
         this thread and false otherwise.  (logical)

.N SignalSafe

.N Fortran

.N Errors
.N MPI_SUCCESS
@*/
int MPI_Is_thread_main(int *flag)
{
    *flag = 1;

    return 0;
}

/* vi:set ts=4 sw=4 tw=76 expandtab: */
