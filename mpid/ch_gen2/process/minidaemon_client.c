#include <stdio.h>
#include "minidaemon.h"

#define NAME_INDEX         1
#define NUM_INDEX          2
#define WIDTH_INDEX        3
#define REM_SH_COM_INDEX   4
#define REM_SH_ARGS_INDEX  5
#define ROOT_NAME          6
#define ROOT_CH_NUM        7
#define MPIRUN_PORT_INDEX  8
#define PID_INDEX          9 
#define ARRAY_IT_INDEX    10
#define WD_INDEX          11
#define NUM_PARAMS_INDEX  12

#define PREFIX             13
#define DEFAULT_PARAM_NUMBER (PREFIX + 1)

int main (int argc, char * argv[]) {
	int width;
    char *mpi_params = NULL;
    char *command = NULL;

    MD_PRINT(DDEBUG,"client main: Starting\n");
    /* some basic checks */
	if (argc < DEFAULT_PARAM_NUMBER) {
		MD_USR_ERROR("Minidaemon client : too few parameters to run minidaemon");
        exit(MD_EXIT_SYS_ERROR);
	}
	if ( (width = atoi(argv[WIDTH_INDEX])) < 1) {
		MD_USR_ERROR("Minidaemon client : invalid tree width received");
        exit(MD_EXIT_SYS_ERROR);
	}

    mpi_params = md_argv_to_string(PREFIX, PREFIX + atoi(argv[NUM_PARAMS_INDEX]), argv);
    if (NULL == mpi_params) {
        MD_SYS_ERROR("Failed to allocate memory for mpi_params\n");
        exit(MD_EXIT_SYS_ERROR);
    }

    command = md_argv_to_string(PREFIX + atoi(argv[NUM_PARAMS_INDEX]), argc, argv);
    if (NULL == command) {
        MD_SYS_ERROR("Failed to allocate memory for mpi_params\n");
        exit(MD_EXIT_SYS_ERROR);
    }
    minidaemon_init (argv[NAME_INDEX], atoi(argv[NUM_INDEX]), atoi(argv[WIDTH_INDEX]),
            argv[REM_SH_COM_INDEX], argv[REM_SH_ARGS_INDEX], argv[ROOT_NAME], atoi(argv[ROOT_CH_NUM]), 
            atoi(argv[MPIRUN_PORT_INDEX]), atoi(argv[PID_INDEX]), atoi(argv[ARRAY_IT_INDEX]),
            argv[WD_INDEX], atoi(argv[NUM_PARAMS_INDEX]), mpi_params, command);
            
	minidaemon_run();
	return 0;
}
