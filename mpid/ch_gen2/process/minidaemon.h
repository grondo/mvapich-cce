#ifndef _MINIDAEMON_H
#define _MINIDAEMON_H 1

/**
* Minidaemon ADT  provided by Mellanox, MPI Team
* mailto : xalex@mellanox.co.il
*
* Minidaemon interface provides the possibilty to create and run
* daemon process on clusters node. Each instance of it will be created 
* at application run time and will destroyed right after application termination.
* Each minidaemon instance may run user-defined jobs on a local machine as well as launch other minidaemons on remote machines.
* So, the user can define the execution scheme ("launch tree") for his needs. The concrete
* Thus, Minidaemon provide an answer to the following issues :
  1. Multicore management. It's only one daemon per machine that will start all jobs there 
  2. Scalable and fast launch of (MPI) jobs on a cluster, using different start-up schemes (flat, multilevel tree)
  3. Cluster job management and cleanup (this version is ad-hoc MPI tuned)
  4. Ease of use and simplicity for cluster administrators. There's no constant daemon that requires 
  	 additional management and resources
**/

#include "mpirun_rsh.h"


#define MD_USR_ERROR(msg) 			\
	do {							\
		fprintf(stderr,"%s\n",msg); \
		exit(1);					\
	} while (0);

#define MD_SYS_ERROR(msg) 			\
		do {						\
		perror(msg); 				\
		exit(MD_EXIT_SYS_ERROR);	\
	} while (0);

/* Minidaemon exit statuses */
typedef enum {
	MD_EXIT_NORMAL = 0,			/* use this exit status when all user apllications finished successfully */
	MD_EXIT_SYS_ERROR = 1, 			/* use this exit status when system error was happen */
	MD_EXIT_APP_FAIL = -1,			/* use this exit status when user application(s) exited with bad status */
	MD_EXIT_MINIDAEMON_FAIL = -2,	/* use this exit status when md failed or there was timeout on connection */
	MD_EXIT_MINIDAEMON_SIG = -3	/* use this exit status when md failed or there was timeout on connection */
} md_exit_status_type;

/* Support for debug prints. */

#define DNONE  0 /* Message with this debug level will be always printed */
#define DINFO  1 /* Info messages level is applicable for rare and informative messages */
#define DDEBUG 2 /* Debug level, should be turned off in normal run */
#define DPATH  3 /* The highest level, for use in loops or critical sections */

#ifndef DGLOBAL_LEVEL
#define DGLOBAL_LEVEL -1
#endif

/* DGLOBAL_LEVEL should be defined in compilation stage, otherwise it should be zero */
#if DGLOBAL_LEVEL >= 0
#define MD_PRINT(dlevel,fmt, args...)   {if (dlevel <= DGLOBAL_LEVEL){\
    fprintf(stderr, "[%s:%d, pid=%d]", __FILE__, __LINE__,getpid());\
    fprintf(stderr, fmt, ## args); fflush(stderr);}}
#else
#define  MD_PRINT(dlevel,fmt, args...)
#endif

/**
Minidaemon ADT  provided by Mellanox, MPI Team

Minidaemon interface provides the possibilty to create and run
daemon process on clusters node. Each instance of it will be created 
at application run time and will destroyed right after application termination.
**/
/*typedef enum {
    P_NOTSTARTED,
    P_STARTED,
    P_CONNECTED,
    P_DISCONNECTED,
    P_RUNNING,
    P_FINISHED,
    P_EXITED
} process_state;

struct process_t {
    char *hostname;
    char *device;
    pid_t pid;
    pid_t remote_pid;
    int port;
    int control_socket;
    process_state state;
} ;*/

/* Minidaemon Data Structures */

typedef struct childrenList_t * ChildrenList;
typedef struct md_entry_t * MD_entry;
typedef struct minidaemon_t * Minidaemon;
/*typedef struct process_t process;*/

void minidaemon_create(process * procList, int nproc, int width, int mpirun_port, int ppid,
        const char * remote_sh_command, const char * remote_sh_args, char * wd, int num_of_params,
        const char * mpi_params, const char * command);

/* Init all data member structures */
void minidaemon_init ( const char * par_name , int ch_num, int width,  const char * remote_sh_command, 
        const char * remote_sh_args,const char * root_host , int root_ch_num, int mpirun_port, 
        int ppid, int array_it, char * wd, int num_params, const char * mpi_params, const char * command); 

/* Start listening to messages from other Minidaemons */
void minidaemon_run ();
char * md_argv_to_string(int start, int end, char **argv );

#endif /* _MINIDAEMON_H */
