#include "mpi.h"

#ifdef HAVE_WEAK_SYMBOLS

/*
 * Undefing MPI_Query_thread if mpi.h defined it to help catch
 * library/headerfile conflicts
 */
#ifdef MPI_Query_thread
#   undef MPI_Query_thread
#   undef PMPI_Query_thread
#endif

#if defined(HAVE_PRAGMA_WEAK)
#   pragma weak MPI_Query_thread = PMPI_Query_thread
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#   pragma _HP_SECONDARY_DEF PMPI_Query_thread  MPI_Query_thread
#elif defined(HAVE_PRAGMA_CRI_DUP)
#   pragma _CRI duplicate MPI_Query_thread as PMPI_Query_thread
#endif

/* Include mapping from MPI->PMPI */
#define MPI_BUILD_PROFILING
#include "mpiprof.h"
/* Insert the prototypes for the PMPI routines */
#undef __MPI_BINDINGS
#include "binding.h"
#endif /* HAVE_WEAK_SYMBOLS */

/*@
   MPI_Query_thread - Return the level of thread support provided by the MPI 
    library

   Output Parameter:
.  provided - Level of thread support provided.  This is the same value
   that was returned in the 'provided' argument in 'MPI_Init_thread'.

   Notes:
   The valid values for the level of thread support are\:
+ MPI_THREAD_SINGLE - Only one thread will execute. 
. MPI_THREAD_FUNNELED - The process may be multi-threaded, but only the main 
  thread will make MPI calls (all MPI calls are funneled to the 
   main thread). 
. MPI_THREAD_SERIALIZED - The process may be multi-threaded, and multiple 
  threads may make MPI calls, but only one at a time: MPI calls are not 
  made concurrently from two distinct threads (all MPI calls are serialized). 
- MPI_THREAD_MULTIPLE - Multiple threads may call MPI, with no restrictions. 

   If 'MPI_Init' was called instead of 'MPI_Init_thread', the level of
   thread support is defined by the implementation.  This routine allows
   you to find out the provided level.  It is also useful for library 
   routines that discover that MPI has already been initialized and
   wish to determine what level of thread support is available.

.N SignalSafe

.N Fortran

.N Errors
.N MPI_SUCCESS
@*/
int MPI_Query_thread(int *provided)
{
    *provided = MPI_THREAD_FUNNELED;

    return 0;
}

/* vi:set ts=4 sw=4 tw=76 expandtab: */
