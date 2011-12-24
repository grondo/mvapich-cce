/*RAM
 * Copyright (C) 1999-2001 The Regents of the University of California
 * (through E.O. Lawrence Berkeley National Laboratory), subject to
 * approval by the U.S. Department of Energy.
 *
 * Use of this software is under license. The license agreement is included
 * in the file MVICH_LICENSE.TXT.
 *
 * Developed at Berkeley Lab as part of MVICH.
 *
 * Authors: Bill Saphir      <wcsaphir@lbl.gov>
 *          Michael Welcome  <mlwelcome@lbl.gov>
 */

/* Copyright (c) 2002-2010, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT_MVAPICH in the top level MPICH directory.
 *
 */

/*
 * ==================================================================
 * This file contains the source for a simple MPI process manager
 * used by MVICH.
 * It simply collects the arguments and execs either RSH or SSH
 * to execute the processes on the remote (or local) hosts.
 * Some critical information is passed to the remote processes
 * through environment variables using the "env" utility. 
 *
 * The information passed through the environment variables is:
 *  MPIRUN_HOST = host running this mpirun_rsh command
 *  MPIRUN_PORT = port number mpirun_rsh is listening on for TCP connection
 *  MPIRUN_RANK = numerical MPI rank of remote process
 *  MPIRUN_NPROCS = number of processes in application
 *  MPIRUN_ID   = pid of the mpirun_rsh process
 *
 * The remote processes use this to establish TCP connections to
 * this mpirun_rsh process.  The TCP connections are used to exchange
 * address data needed to establish the VI connections.
 * The TCP connections are also used for a simple barrier syncronization
 * at process termination time.
 *
 * MVICH allows for the specification of certain tuning parameters
 * at run-time.  These parameters are read by mpirun_rsh from a
 * file given on the command line.  Currently, these parameters are
 * passed to the remote processes through environment variables, but 
 * they could be sent as a string over the TCP connection.  It was
 * thought that using environment variables might be more portable
 * to other process managers.
 * ==================================================================
 */

#include "mpirun_rsh.h"
#include "minidaemon.h"
#include <math.h>
#include "mpispawn_tree.h"
#include "mpirun_util.h"

process_groups * pglist = NULL;
process * plist = NULL;
int nprocs = 0;
int use_xlauncher = 0;
int xlauncher_width = 8;
int aout_index, port;
char *wd;            /* working directory of current process */
#define MAX_HOST_LEN 256
char mpirun_host[MAX_HOST_LEN]; /* hostname of current process */
/* xxx need to add checking for string overflow, do this more carefully ... */
char * mpispawn_param_env = NULL;
int param_count = 0, legacy_startup = 0;
#define ENV_LEN 20480
#define LINE_LEN 256



/*
 * Message notifying user of what timed out
 */
static const char * alarm_msg = NULL;

void free_memory(void);
void pglist_print(void);
void pglist_insert(const char * const, const int);
void rkill_fast(void);
void rkill_linear(void);
void spawn_fast(int, char *[], char *, char *);
void spawn_linear(int, char *[], char *, char *);
void cleanup_handler(int);
void nostop_handler(int);
void alarm_handler(int);
void child_handler(int);
void usage(void);
int start_process(int i, char *command_name, char *env);
void cleanup(void);
char *skip_white(char *s);
int read_param_file(char *paramfile,char **env);
void wait_for_mpispawn_errors(int s,struct sockaddr_in *sockaddr,
        unsigned int sockaddr_len);
void wait_for_errors(int s,struct sockaddr *sockaddr,
        unsigned int sockaddr_len);
int set_fds(fd_set * rfds, fd_set * efds);
static int read_cmdline_to_env(char **env, char *argv[]);
static int read_hostfile(char *hostfile_name);
void make_command_strings(int argc, char * argv[], char * totalview_cmd, char * command_name, char * command_name_tv);
void mpispawn_checkin(int, struct sockaddr *, unsigned int);


#ifdef USE_SSH
int use_rsh = 0;
#else
int use_rsh = 1;
#endif

static struct option option_table[] = {
    {"np", required_argument, 0, 0},
    {"debug", no_argument, 0, 0},
    {"xterm", no_argument, 0, 0},
    {"hostfile", required_argument, 0, 0},
    {"paramfile", required_argument, 0, 0},
    {"show", no_argument, 0, 0},
    {"rsh", no_argument, 0, 0},
    {"ssh", no_argument, 0, 0},
    {"help", no_argument, 0, 0},
    {"v", no_argument, 0, 0},
    {"tv", no_argument, 0, 0},
    {"legacy", no_argument, 0, 0},
	{"use_xlauncher", no_argument, 0, 0},
	{"xlauncher_width", required_argument, 0, 0},
    {"startedByTv", no_argument, 0, 0},
    {0, 0, 0, 0}
};

static void show_version(void)
{
    fprintf(stderr,"OSU MVAPICH VERSION %s-SingleRail\n"
            "Build-ID: %s\n", MVAPICH_VERSION, MVAPICH_BUILDID);
}

int debug_on = 0, xterm_on = 0, show_on = 0;
int param_debug = 0;
int use_totalview = 0;
int server_socket;
char display[200];
char * binary_dirname;
int use_dirname = 1;

static inline int env2int(char * env_ptr) {
    return (env_ptr = getenv(env_ptr)) ? atoi(env_ptr) : 0;
}


static void get_display_str()
{
    char *p;
    char str[200];

    if ( (p = getenv( "DISPLAY" ) ) != NULL ) {
	strcpy(str, p ); /* For X11 programs */  
	sprintf(display,"DISPLAY=%s",str); 	
    }
}

/* Start mpirun_rsh totalview integration */

#define MPIR_DEBUG_SPAWNED                1
#define MPIR_DEBUG_ABORTING               2

struct MPIR_PROCDESC *MPIR_proctable    = 0;
int MPIR_proctable_size                 = 0;
int MPIR_i_am_starter                   = 1;
int MPIR_debug_state                    = 0;
char *MPIR_dll_name                     = "MVAPICH";
/* Totalview intercepts MPIR_Breakpoint */
int MPIR_Breakpoint (void)
{
    return 0;
}

/* End mpirun_rsh totalview integration */

int main(int argc, char *argv[])
{
    int i, s, s1, c, option_index;
    int hostfile_on = 0;
#define HOSTFILE_LEN 256
    char hostfile[HOSTFILE_LEN + 1];
    int paramfile_on = 0;
#define PARAMFILE_LEN 256
    char paramfile[PARAMFILE_LEN + 1];
    char *param_env;
    struct sockaddr_in sockaddr;
    unsigned int sockaddr_len = sizeof(sockaddr);

    char *env = "\0";
    int num_of_params = 0;

    char totalview_cmd[200];
    char *tv_env;

    int version;
    int timeout;

    size_t hostname_len = 0;
    totalview_cmd[199] = 0;
    display[0]='\0';	

    /* mpirun [-debug] [-xterm] -np N [-hostfile hfile | h1 h2 h3 ... hN] a.out [args] */

    atexit(free_memory);

    do {
        c = getopt_long_only(argc, argv, "+", option_table, &option_index);
        switch (c) {
        case '?':
        case ':':
            usage();
	    exit(EXIT_FAILURE);
            break;
        case EOF:
            break;
        case 0:
            switch (option_index) {
            case 0:
                nprocs = atoi(optarg);
                if (nprocs < 1) {
                    usage();
		    exit(EXIT_FAILURE);
		}
                break;
            case 1:
                debug_on = 1;
                xterm_on = 1;
                legacy_startup = 1;
                break;
            case 2:
                xterm_on = 1;
                break;
            case 3:
                hostfile_on = 1;
                strncpy(hostfile, optarg, HOSTFILE_LEN);
                if (strlen(optarg) >= HOSTFILE_LEN - 1)
                    hostfile[HOSTFILE_LEN] = '\0';
                break;
            case 4:
                paramfile_on = 1;
                strncpy(paramfile, optarg, PARAMFILE_LEN);
                if (strlen(optarg) >= PARAMFILE_LEN - 1) {
                    paramfile[PARAMFILE_LEN] = '\0';
                }
                break;
            case 5:
                show_on = 1;
                break;
            case 6:
                use_rsh = 1;
                break;
            case 7:
                use_rsh = 0;
                break;
            case 8:
                show_version();
                usage();
                exit(EXIT_SUCCESS);
                break;
            case 9:
                show_version();
                exit(EXIT_SUCCESS);
                break;
            case 10: 
                {
                    /* -tv */
                    int count, idx;
                    char **new_argv;
 		            tv_env = getenv("TOTALVIEW");
 		            if (tv_env != NULL) {
		                strcpy (totalview_cmd,tv_env);	
 		            } 
                    else {
		                fprintf (stderr,
			                    "TOTALVIEW env is NULL, use default: %s\n", 
			                    TOTALVIEW_CMD);
		                sprintf (totalview_cmd, "%s", TOTALVIEW_CMD);
 		            }
                    new_argv = (char **) malloc (sizeof (char **) * argc + 3);
                    new_argv[0] = totalview_cmd;
                    new_argv[1] = argv[0];
                    new_argv[2] = "-a";
                    new_argv[3] = "-startedByTv";
                    idx = 4;
                    for (count = 1; count < argc; count ++) {
                        if (strcmp (argv[count], "-tv"))
                            new_argv[idx++] = argv [count];
                    }
                    new_argv[idx] = NULL;
                    if (execv (new_argv[0], new_argv)) {
                        perror ("execv");
                        exit (EXIT_FAILURE);
                    }
                    
                }
  		        break;
	        case 11:
                if (!use_totalview)
		            legacy_startup = 1;
                else {
                    fprintf (stderr, "Totalview debugger not supported"
                            " with legacy mode\n");
                    usage ();
                    exit (EXIT_FAILURE);
                }
		        break;
 	        case 14:
                /* -startedByTv */
 		        use_totalview = 1;
		        debug_on = 1;
                break;
	    case 12:
		use_xlauncher = 1;
		break;
	    case 13:
		xlauncher_width = atoi(optarg);
		if (xlauncher_width < 1) {
		    usage();
		    exit(EXIT_FAILURE);
		}
		break;
	    case 15:
		usage();
		exit(EXIT_SUCCESS);
		break;
	    default:
		fprintf(stderr, "Unknown option\n");
                usage();
		exit(EXIT_FAILURE);
		break;
	    }
            break;
        default:
            fprintf(stderr, "Unreachable statement!\n");
            usage();
	    exit(EXIT_FAILURE);
            break;
        }
    } while (c != EOF);

    if(!nprocs) {
	usage();
	exit(EXIT_FAILURE);
    }
    
    binary_dirname = strdup(dirname(argv[0]));
    if (strlen (binary_dirname) == 1 && argv[0][0] != '.') {
        use_dirname = 0;
    }

    if (!hostfile_on) {
        /*
         *  The user has not specified an hostfile.
         */
        if (argc - optind == 1)
        {

        	//It tries to have the hostfile from the environemnt variable.
        	if ( getenv("MPIRUN_HOSTFILE") != NULL )
        	{
        		strncpy (hostfile, getenv("MPIRUN_HOSTFILE"), HOSTFILE_LEN);
        		if (strlen(getenv("MPIRUN_HOSTFILE")) >= HOSTFILE_LEN - 1)
        			hostfile[HOSTFILE_LEN] = '\0';
        	}
        	/*
        	 * The environment variable is not specified.
        	 * It tries to get hostfile from the default location.
        	 * The defualt location is $HOME/.mpirun_hostfile.
        	 */
        	else
        	{
        		int dim_home = strlen(getenv("HOME"));
        		char * home_dir = malloc( dim_home + 1 );
        		home_dir = getenv("HOME");
        		strncpy (hostfile, home_dir, HOSTFILE_LEN);

        		if (dim_home >= HOSTFILE_LEN - 1)
        			hostfile[HOSTFILE_LEN] = '\0';
        		if ( ( dim_home + strlen(DEFAULT_HOSTFILE_NAME) ) < HOSTFILE_LEN )
        		{
        				strcat(hostfile, DEFAULT_HOSTFILE_NAME);
        		}

        		else
        		{
        			fprintf(stderr, "Hostfile name too long. Max leng. 256\n");
        			exit(EXIT_FAILURE);
        		}

        	}
        	hostfile_on = 1;

        }
        else if (argc - optind < nprocs + 1) {
            aout_index = optind;
        }
        else
        {
        	aout_index = nprocs + optind;
        }


    }



    /* reading default param file */
    if ( 0 == (access(PARAM_GLOBAL, R_OK))) {
	    num_of_params += read_param_file(PARAM_GLOBAL, &env);
    }

    /* reading file specified by user env */
    if (( param_env = getenv("MVAPICH_DEF_PARAMFILE")) != NULL ){
	    num_of_params += read_param_file(param_env, &env);
    }

    if (paramfile_on) {
        /* construct a string of environment variable definitions from
         * the entries in the paramfile.  These environment variables
         * will be available to the remote processes, which
         * will use them to over-ride default parameter settings
         */
        num_of_params += read_param_file(paramfile, &env);
    }
	    	
    plist = malloc(nprocs * sizeof(process));
    if (plist == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < nprocs; i++) {
        plist[i].state = P_NOTSTARTED;
        plist[i].device = NULL;
        plist[i].port = -1;
	plist[i].remote_pid = 0;
    }

    /* grab hosts from command line or file */

    if (hostfile_on) {
    	aout_index = optind;
        hostname_len = read_hostfile(hostfile);

    } else {
        for (i = 0; i < nprocs; i++) {
            plist[i].hostname = (char *)strndup(argv[optind + i], 100);
    	    hostname_len = hostname_len > strlen(plist[i].hostname) ?
    		    hostname_len : strlen(plist[i].hostname); 
        }
    }

    if (use_totalview) { 
        MPIR_proctable = (struct MPIR_PROCDESC *) malloc 
                (MPIR_PROCDESC_s * nprocs);
        MPIR_proctable_size = nprocs;

        for (i = 0; i < nprocs; ++i) {
            MPIR_proctable[i].host_name = plist[i].hostname;
        }
    }
   
    wd = get_current_dir_name();
    gethostname(mpirun_host, MAX_HOST_LEN);

    get_display_str();

    server_socket = s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0) {
	perror("socket");
	exit(EXIT_FAILURE);
    }
    sockaddr.sin_addr.s_addr = INADDR_ANY;
    sockaddr.sin_port = 0;
    if (bind(s, (struct sockaddr *) &sockaddr, sockaddr_len) < 0) {
	perror("bind");
	exit(EXIT_FAILURE);
    }

    if (getsockname(s, (struct sockaddr *) &sockaddr, &sockaddr_len) < 0) {
	perror("getsockname");
	exit(EXIT_FAILURE);
    }

    port = (int) ntohs(sockaddr.sin_port);
    listen(s, nprocs);

    if (!show_on) {
	struct sigaction signal_handler;
	signal_handler.sa_handler = cleanup_handler;
	sigfillset(&signal_handler.sa_mask);
	signal_handler.sa_flags = 0;

	sigaction(SIGHUP, &signal_handler, NULL);
	sigaction(SIGINT, &signal_handler, NULL);
	sigaction(SIGTERM, &signal_handler, NULL);

	signal_handler.sa_handler = nostop_handler;

	sigaction(SIGTSTP, &signal_handler, NULL);

	signal_handler.sa_handler = alarm_handler;

	sigaction(SIGALRM, &signal_handler, NULL);

	if (!use_xlauncher) {
	    signal_handler.sa_handler = child_handler;
	    sigemptyset(&signal_handler.sa_mask);

	    sigaction(SIGCHLD, &signal_handler, NULL);
	}
    }

    for (i = 0; i < nprocs; i++) {
		/*
		 * I should probably do some sort of hostname lookup to account for
		 * situations where people might use two different hostnames for the
		 * same host.
		 */
		pglist_insert(plist[i].hostname, i);
    }

    timeout = env2int ("MPIRUN_TIMEOUT");
    if (timeout <= 0) {
        timeout = pglist ? pglist->npgs: nprocs;
        if (timeout < 30) timeout = 30;
        else if (timeout > 1000) timeout = 1000;
        if (debug_on) {
            /* Timeout of 24 hours so that we don't interrupt debugging */
            timeout += 86400;
        }
    }
    alarm(timeout);
    alarm_msg = "Timeout during client startup.\n";

	int md_id;	
    if (use_xlauncher) {
	md_id = fork();
	if (md_id == 0) {
        assert (NULL != env);
	    char command_name[COMMAND_LEN];
	    char command_name_tv[COMMAND_LEN];
	    char *ld_library_path;
	    char *mpi_prefix;

        /* For minidemon we must to pass some parameter */
        num_of_params += read_cmdline_to_env(&env, argv);
	    make_command_strings(argc, argv, totalview_cmd, command_name, command_name_tv);
	    ld_library_path = getenv("LD_LIBRARY_PATH");
        if (ld_library_path != NULL) {
            fprintf(stderr,"<mpirun_rsh> Setting LD_LIBRARY_PATH = %s\n",ld_library_path);
            setenv("LD_LIBRARY_PATH",ld_library_path,1);
        }

        mpi_prefix =  getenv("MPI_PREFIX");
        if (mpi_prefix != NULL) {
            fprintf(stderr,"<mpirun_rsh> Setting MPI_PREFIX = %s\n",mpi_prefix);
            setenv("MPI_PREFIX",mpi_prefix,1);
        }
        minidaemon_create(plist, nprocs, xlauncher_width, port, getpid(), 
                use_rsh ? RSH_CMD : SSH_CMD, use_rsh ? RSH_ARG : SSH_ARG, wd, 
                strlen(env) ? num_of_params : 1,
                strlen(env) ? env : "VIADEV_PARAM=DEMO_PARAM",
                command_name);

	    minidaemon_run(); 
		/* minidaemon should not reach this line in normal flow,
			only special signal can be the reason */
		exit(MD_EXIT_MINIDAEMON_SIG);
	}
    }
	
    else if(pglist && !legacy_startup) {
	spawn_fast(argc, argv, totalview_cmd, env);

	if(show_on) exit(EXIT_SUCCESS);

	mpispawn_checkin(server_socket, (struct sockaddr *)&sockaddr,
		sockaddr_len);
	alarm(0);
	wait_for_mpispawn_errors (server_socket, &sockaddr, sockaddr_len);

	/*
	 * This exit should never be reached.
	 */
	exit(EXIT_FAILURE);
    }

    spawn_linear(argc, argv, totalview_cmd, env);

    if(show_on) exit(EXIT_SUCCESS);

    /* build up an array of file descriptors for pmgr_processops */
    int* fds = (int*) malloc(nprocs*sizeof(int));

    if(fds == NULL) {
	perror("allocating temporary array for socket file descriptors");
	cleanup();
    }

    /* accept incoming connections */

    for (i = 0; i < nprocs; i++) {
	int rank;
ACCEPT_HID:
	sockaddr_len = sizeof(sockaddr);
	s1 = accept(s, (struct sockaddr *) &sockaddr, &sockaddr_len);

	alarm_msg = "Timeout during hostid exchange.\n";

	if (s1 < 0) {
	    if ((errno == EINTR) || (errno == EAGAIN))
		goto ACCEPT_HID;
	    perror("accept");
	    cleanup();
	}

	/*
	 * protocol:
	 *  0. read protocol version number
	 *  1. read rank of process
	 */

	/* 0. Find out what version of the startup protocol the executable
	 * was compiled to use. */

	if(read_socket(s1, &version, sizeof(version))) cleanup ();

	if(version != PMGR_VERSION) {
	    fprintf(stderr, "mpirun: executable version %d does not match"
		    " our version %d.\n", version, PMGR_VERSION);
	    cleanup();
	}

	/* 1. Find out who we're talking to */
	if(read_socket(s1, &rank, sizeof(rank))) cleanup ();

	if (rank < 0 || rank >= nprocs || 
		( !(use_xlauncher) && plist[rank].state != P_STARTED)) {
	    fprintf(stderr, "mpirun: invalid rank received. \n");
	    cleanup();
	}

	fds[rank] = plist[rank].control_socket = s1;
    }
    /* at this point, all processes have checked in hostids */
    /* cancel the timeout */
    alarm(0);

    pmgr_processops(fds, nprocs);

    /* free it off (processops closes each socket before returning control) */
    free(fds);

    for (i = 0; i < nprocs; i++) {
	plist[i].state = P_RUNNING;
    }

    if(use_xlauncher) {	
	int status;

	waitpid(md_id, &status, 0);
	exit(WIFEXITED(status) ? WEXITSTATUS (status) : MD_EXIT_MINIDAEMON_SIG);
    }

    wait_for_errors (s, (struct sockaddr *)&sockaddr, sockaddr_len);

    /*
     * This return should never be reached.
     */
    return EXIT_FAILURE;
}

int remote_host(char *rhost)
{
    char lhost[64];

    if (!strcmp(rhost, "localhost") || !strcmp(rhost, "127.0.0.1"))
        return 0;
    if (!gethostname(lhost, sizeof(lhost)) && !strcmp(rhost, lhost))
        return 0;
    return 1;
}

char *lookup_shell(void)
{
    char *shell = getenv("SHELL");

    return strdup(shell ? shell : SH_CMD);
}

int start_process(int i, char *command_name, char *env)
{
    char * remote_command = NULL;
    char * xterm_command = NULL;
    char * xterm_title = NULL;
    int use_sh = !remote_host(plist[i].hostname);
    char * sh_cmd = lookup_shell();

    int id = getpid();

    remote_command = mkstr("cd %s; %s LD_LIBRARY_PATH=%s", wd, ENV_CMD, LD_LIBRARY_PATH_MPI);

    if(getenv("LD_LIBRARY_PATH")) {
	remote_command = append_str(remote_command, mkstr(":%s",
		    getenv("LD_LIBRARY_PATH")));
    }

    remote_command = append_str(remote_command, mkstr(" MPIRUN_MPD=0"));
    remote_command = append_str(remote_command, mkstr(" MPIRUN_HOST=%s",
		mpirun_host));
    remote_command = append_str(remote_command, mkstr(" MPIRUN_PORT=%d", port));
    remote_command = append_str(remote_command, mkstr(" MPIRUN_RANK=%d", i));
    remote_command = append_str(remote_command, mkstr(" MPIRUN_NPROCS=%d",
		nprocs));
    remote_command = append_str(remote_command, mkstr(" MPIRUN_ID=%d", id));
    remote_command = append_str(remote_command, mkstr(" %s %s", display, env));

    if(plist[i].device) {
	remote_command = append_str(remote_command, mkstr(" VIADEV_DEVICE=%s",
		    plist[i].device));
    }

    if(plist[i].port != -1) {
	remote_command = append_str(remote_command,
		mkstr(" VIADEV_DEFAULT_PORT=%d", plist[i].port));
    }

    remote_command = append_str(remote_command, mkstr(" %s", command_name));

    if (xterm_on) {
	xterm_command = mkstr("%s; echo process exited", remote_command);
	xterm_title = mkstr("\"mpirun process %d of %d\"", i, nprocs);
    }

    plist[i].pid = fork();
    plist[i].state = P_STARTED; 
    /* putting after fork() avoids a race */
   
    if (plist[i].pid == 0) {
        if (i != 0) {
            int fd = open("/dev/null", O_RDWR, 0);
            (void) dup2(fd, STDIN_FILENO);
        }

        if (xterm_on) {
            if (show_on) {
                if (use_sh) {
                    printf("command: %s -T %s -e %s %s \"%s\"\n", XTERM,
                           xterm_title, sh_cmd, SH_ARG,
                           xterm_command);
                } else if (use_rsh) {
                    printf("command: %s -T %s -e %s %s %s\n", XTERM,
                           xterm_title, RSH_CMD, plist[i].hostname,
                           xterm_command);
                } else { /* ssh */
                    printf("command: %s -T %s -e %s %s %s %s\n", XTERM,
                           xterm_title, SSH_CMD, SSH_ARG, plist[i].hostname,
                           xterm_command);
                }
            } else {
                if (use_sh) {
                     execl(XTERM, XTERM, "-T", xterm_title, "-e",
                          sh_cmd, SH_ARG, xterm_command, NULL);
                } else if (use_rsh) {
                    execl(XTERM, XTERM, "-T", xterm_title, "-e",
                          RSH_CMD, plist[i].hostname, xterm_command, NULL);
                } else { /* ssh */
                     execl(XTERM, XTERM, "-T", xterm_title, "-e",
                          SSH_CMD, SSH_ARG, plist[i].hostname,
                          xterm_command, NULL);
                }
            }
        } else {
            if (show_on) {
                if (use_sh) {
                    printf("command: %s %s \"%s\"\n", sh_cmd, SH_ARG,
                           remote_command);
                } else if (use_rsh) {
                    printf("command: %s %s %s\n", RSH_CMD,
                           plist[i].hostname, remote_command);
                } else { /* ssh */
                    printf("command: %s %s %s %s\n", SSH_CMD, SSH_ARG,
                           plist[i].hostname, remote_command);
                }
            } else {
                if (use_sh) {
                    execl(sh_cmd, sh_cmd, SH_ARG,
                          remote_command, NULL);
                } else if (use_rsh) {
                    execl(RSH_CMD, RSH_CMD, plist[i].hostname,
                          remote_command, NULL);
                } else { /* ssh */
                    execl(SSH_CMD, SSH_CMD, SSH_ARG, plist[i].hostname,
                          remote_command, NULL);
                }
            }
        }
        if (!show_on) {
            perror("RSH/SSH command failed!");
        }
        exit(EXIT_FAILURE);
    }

    free(remote_command);
    free(xterm_command);
    free(xterm_title);
    free(sh_cmd);
    return (0);
}

void wait_for_errors(int s, struct sockaddr *sockaddr, unsigned int
	sockaddr_len)
{
    int wfe_socket, wfe_abort_code, wfe_abort_rank, wfe_abort_msglen;

    while((wfe_socket = accept(s, sockaddr, &sockaddr_len)) < 0) {
	if(errno == EINTR || errno == EAGAIN) continue;

	perror("accept");
	cleanup();
    }

    if(read_socket(wfe_socket, &wfe_abort_code, sizeof(int))
	    || read_socket(wfe_socket, &wfe_abort_rank, sizeof(int))
	    || read_socket(wfe_socket, &wfe_abort_msglen, sizeof(int))) {
	fprintf(stderr, "Termination socket read failed!\n");
	cleanup();
    }

    else {
	char wfe_abort_message[wfe_abort_msglen];

	fprintf(stderr, "Abort signaled from %s (rank %d): ",
		plist[wfe_abort_rank].hostname, wfe_abort_rank);

	if(!read_socket(wfe_socket, &wfe_abort_message, wfe_abort_msglen)) {
	    fprintf(stderr, "%s\n", wfe_abort_message); 
	}

	cleanup();
    }
}

void wait_for_mpispawn_errors (int s, struct sockaddr_in *sockaddr, 
        unsigned int sockaddr_len)
{
    int wfe_socket, wfe_abort_code, wfe_abort_mid;
    
    while((wfe_socket = accept(s, (struct sockaddr *) sockaddr, 
		    &sockaddr_len)) < 0) {
	if(errno == EINTR || errno == EAGAIN) continue;
	perror("accept");
	cleanup();
    }
   
    if(read_socket(wfe_socket, &wfe_abort_code, sizeof(int))
	    || read_socket(wfe_socket, &wfe_abort_mid, sizeof(int))) {
	fprintf(stderr, "Termination socket read failed!\n");
    }
    else {
	fprintf(stderr, "Exit code %d signaled from %s\n", wfe_abort_code, 
		pglist->index[wfe_abort_mid]->hostname);
    }
   
    close (wfe_socket);
    cleanup();
}

void usage(void)
{
    fprintf(stderr, "usage: mpirun_rsh [-v] [-rsh|-ssh] "
            "[-paramfile=pfile] "
  	    "[-debug] -[tv] [-xterm] [-show] [-legacy] [-use_xlauncher]"
		"[xlauncher-width W] -np N "
            "(-hostfile hfile | h1 h2 ... hN) a.out args\n");
    fprintf(stderr, "Where:\n");
    fprintf(stderr, "\tv          => Show version and exit\n");
    fprintf(stderr, "\trsh        => " "to use rsh for connecting\n");
    fprintf(stderr, "\tssh        => " "to use ssh for connecting\n");
    fprintf(stderr, "\tparamfile  => "
            "file containing run-time MVICH parameters\n");
    fprintf(stderr, "\tdebug      => "
            "run each process under the control of gdb\n");
    fprintf(stderr,"\ttv         => "
	    "run each process under the control of totalview\n");
    fprintf(stderr, "\txterm      => "
            "run remote processes under xterm\n");
    fprintf(stderr, "\tshow       => "
            "show command for remote execution but dont run it\n");
    fprintf(stderr, "\tlegacy     => "
	    "use old startup method (1 ssh/process)\n");
    fprintf(stderr, "\tnp         => "
            "specify the number of processes\n");
    fprintf(stderr, "\th1 h2...   => "
            "names of hosts where processes should run\n");
    fprintf(stderr, "or\thostfile   => "
            "name of file contining hosts, one per line. If not specified it tries to have the hostfile "
            "from the environment variable MPIRUN_HOSTFILE if setted up, or from the default file $HOME/.mpirun_rsh \n");
    fprintf(stderr, "\ta.out      => " "name of MPI binary\n");
    fprintf(stderr, "\targs       => " "arguments for MPI binary\n");

    fprintf(stderr, "\n");
}

/* finds first non-whitespace char in input string */
char *skip_white(char *s)
{
    int len;
    /* return pointer to first non-whitespace char in string */
    /* Assumes string is null terminated */
    /* Clean from start */
    while ((*s == ' ') || (*s == '\t'))
        s++;
    /* Clean from end */
    len = strlen(s) - 1; 

    while (((s[len] == ' ') 
                || (s[len] == '\t')) && (len >=0)){
        s[len]='\0';
        len--;
    }
    return s;
}

static int read_cmdline_to_env(char **env, char *argv[])
{
    int env_left, e_len;
    char *buf = NULL;
    int counter = 0;

    if (0 == strlen(*env)) {
        /* Allocating space for env first time */
        if (NULL == (*env = malloc(sizeof(char) * ENV_LEN))) {
            fprintf(stderr, "Malloc of env failed in read_param_file\n");
            exit(EXIT_FAILURE);
        }
        env_left = ENV_LEN - 1;
    } else {
        /* already allocated */
        env_left = ENV_LEN - (strlen(*env) + 1) - 1;
    }

    while(strchr(argv[aout_index], '=')) {
        buf = strdup(argv[aout_index++]);

        e_len = strlen(buf);
        if (e_len > env_left) {
            /* oops, need to grow env string */
            int newlen =
                (ENV_LEN > e_len + 1 ? ENV_LEN : e_len + 1) + strlen(*env);
            if ((*env = realloc(*env, newlen)) == NULL) {
                fprintf(stderr, "realloc failed in read_param_file\n");
                exit(EXIT_FAILURE);
            }
            if (param_debug) {
                printf("realloc to %d\n", newlen);
            }
            env_left = ENV_LEN - 1;
        }
        strcat(*env, " ");
        strcat(*env, buf);
        ++counter;
        env_left -= e_len;
        free(buf);
    }
    return counter;
}

/* Read hostfile */
static int read_hostfile(char *hostfile_name)
{

    size_t j, hostname_len = 0;
    int i;
    FILE *hf = fopen(hostfile_name, "r");

    if (hf == NULL) {
        fprintf(stderr, "Can't open hostfile %s\n", hostfile_name);
        perror("open");
        exit(EXIT_FAILURE);
    }
    
    for (i = 0; i < nprocs; i++) {
        char line[100];
        char *trimmed_line;
        int separator_count = 0,prev_j = 0;

        if (fgets(line, 100, hf) != NULL) {
            size_t len = strlen(line);

            if (line[len - 1] == '\n') {
                line[len - 1] = '\0';
            }
           
            /* Remove comments and empty lines*/
            if (strchr(line, '#') != NULL) {
                line[strlen(line) - strlen(strchr(line, '#'))] = '\0';  
            }
            
            trimmed_line = skip_white(line);
           
            if (strlen(trimmed_line) == 0) {
                /* The line is empty, drop it */
                i--;
                continue;
            }

            /*Update len and continue patch ?! move it to func ?*/
            len = strlen(trimmed_line);
            
            /* Parsing format:
             * hostname SEPARATOR hca_name SEPARATOR port
             */
           
            for (j =0; j < len; j++){
                if ( trimmed_line[j] == SEPARATOR && separator_count == 0){
                    plist[i].hostname = (char *)strndup(trimmed_line, j + 1);
                    plist[i].hostname[j] = '\0';
                    prev_j = j;
                    separator_count++;
                    hostname_len = hostname_len > len ? hostname_len : len;
                    continue;
                }
                if ( trimmed_line[j] == SEPARATOR && separator_count == 1){
                    plist[i].device = (char *)strndup(&trimmed_line[prev_j + 1],
                            j - prev_j);
                    plist[i].device[j-prev_j-1] = '\0';
                    separator_count++;
                    continue;
                }
                if ( separator_count == 2){
                    plist[i].port = atoi(&trimmed_line[j]);
                    break;
                }
            }
            if (0 == separator_count) {
                plist[i].hostname = strdup(trimmed_line);
                hostname_len = hostname_len > len ? hostname_len : len;
            }
            if (1 == separator_count) {
                plist[i].device = (char*)strdup(&trimmed_line[prev_j+1]);
            }
        } else {
            fprintf(stderr, "End of file reached on "
                    "hostfile at %d of %d hostnames\n", i, nprocs);
            exit(EXIT_FAILURE);
        }
    }
    fclose(hf);
    return hostname_len;
}

/*
 * reads the param file and constructs the environment strings
 * for each of the environment variables.
 * The caller is responsible for de-allocating the returned string.
 *
 * NOTE: we cant just append these to our current environment because
 * RSH and SSH do not export our environment vars to the remote host.
 * Rather, the RSH command that starts the remote process looks
 * something like:
 *    rsh remote_host "cd workdir; env ENVNAME=value ... command"
 */
int read_param_file(char *paramfile,char **env)
{
    FILE *pf;
    char errstr[256];
    char name[128], value[193];
    char buf[384];
    char line[LINE_LEN];
    char *p, * tmp;
    int num, e_len;
    int env_left = 0;
    int num_params = 0;

    if ((pf = fopen(paramfile, "r")) == NULL) {
        sprintf(errstr, "Cant open paramfile = %s", paramfile);
        perror(errstr);
        exit(EXIT_FAILURE);
    }

    if ( strlen(*env) == 0 ){
	    /* Allocating space for env first time */
	    if ((*env = malloc(ENV_LEN)) == NULL) {
		    fprintf(stderr, "Malloc of env failed in read_param_file\n");
		    exit(EXIT_FAILURE);
	    }
	    env_left = ENV_LEN - 1;
    }else{
	    /* already allocated */
	    env_left = ENV_LEN - (strlen(*env) + 1) - 1;
    }

    while (fgets(line, LINE_LEN, pf) != NULL) {
        p = skip_white(line);
        if (*p == '#' || *p == '\n') {
            /* a comment or a blank line, ignore it */
            continue;
        }
        /* look for NAME = VALUE, where NAME == MVICH_... */
        name[0] = value[0] = '\0';
        if (param_debug) {
            printf("Scanning: %s\n", p);
        }
        if ((num = sscanf(p, "%64[A-Z_] = %192s", name, value)) != 2) {
            /* debug */
            if (param_debug) {
                printf("FAILED: matched = %d, name = %s, "
                       "value = %s in \n\t%s\n", num, name, value, p);
            }
            continue;
        }

        /* construct the environment string */
        buf[0] = '\0';
        sprintf(buf, "%s=%s ", name, value);
        ++num_params;

	if(mpispawn_param_env) {
	    tmp = mkstr("%s MPISPAWN_GENERIC_NAME_%d=%s"
		    " MPISPAWN_GENERIC_VALUE_%d=%s", mpispawn_param_env,
		    param_count, name, param_count, value);

	    free(mpispawn_param_env);

	    if(tmp) {
		mpispawn_param_env = tmp;
		param_count++;
	    }

	    else {
		fprintf(stderr, "malloc failed in read_param_file\n");
                exit(EXIT_FAILURE);
	    }
	}

	else {
	    mpispawn_param_env = mkstr("MPISPAWN_GENERIC_NAME_%d=%s"
		    " MPISPAWN_GENERIC_VALUE_%d=%s", param_count, name,
		    param_count, value);

	    if(!mpispawn_param_env) {
		fprintf(stderr, "malloc failed in read_param_file\n");
                exit(EXIT_FAILURE);
	    }

	    param_count++;
	}

        /* concat to actual environment string */
        e_len = strlen(buf);
        if (e_len > env_left) {
            /* oops, need to grow env string */
            int newlen =
                (ENV_LEN > e_len + 1 ? ENV_LEN : e_len + 1) + strlen(*env);
            if ((*env = realloc(*env, newlen)) == NULL) {
                fprintf(stderr, "realloc failed in read_param_file\n");
                exit(EXIT_FAILURE);
            }
            if (param_debug) {
                printf("realloc to %d\n", newlen);
            }
            env_left = ENV_LEN - 1;
        }
        strcat(*env, buf);
        env_left -= e_len;
        if (param_debug) {
            printf("Added: [%s]\n", buf);
            printf("env len = %d, env left = %d\n", strlen(*env), env_left);
        }
    }
    fclose(pf);

    return num_params;
}

void cleanup_handler(int sig)
{
    static int already_called = 0;
    printf("Signal %d received.\n", sig);
    if (already_called == 0) {
        already_called = 1;
    }
    cleanup();

    exit(EXIT_FAILURE);
}

void pglist_print(void)
{
    if(pglist) {
	size_t i, j, npids = 0, npids_allocated = 0;

	fprintf(stderr, "\n--pglist--\ndata:\n");
	for(i = 0; i < pglist->npgs; i++) {
	    fprintf(stderr, "%p - %s:", &pglist->data[i],
		    pglist->data[i].hostname);
	    fprintf(stderr, " %d (", pglist->data[i].pid);

	    for(j = 0; j < pglist->data[i].npids; fprintf(stderr, ", "), j++) {
		fprintf(stderr, "%d", pglist->data[i].plist_indices[j]);
	    }

	    fprintf(stderr, ")\n");
	    npids	    += pglist->data[i].npids;
	    npids_allocated += pglist->data[i].npids_allocated;
	}

	fprintf(stderr, "\nindex:");
	for(i = 0; i < pglist->npgs; i++) {
	    fprintf(stderr, " %p", pglist->index[i]);
	}

	fprintf(stderr, "\nnpgs/allocated: %d/%d (%d%%)\n", pglist->npgs,
		pglist->npgs_allocated, (int)(pglist->npgs_allocated ? 100. *
		    pglist->npgs / pglist->npgs_allocated : 100.));
	fprintf(stderr, "npids/allocated: %d/%d (%d%%)\n", npids,
		npids_allocated, (int)(npids_allocated ? 100. * npids /
		    npids_allocated : 100.));
	fprintf(stderr, "--pglist--\n\n");
    }
}

void pglist_insert(const char * const hostname, const int plist_index)
{
    const size_t increment = nprocs > 4 ? nprocs / 4 : 1;
    size_t i, index = 0;
    static size_t alloc_error = 0;
    int strcmp_result, bottom = 0, top;
    process_group * pg;
    void * backup_ptr;

    if(alloc_error) return;
    if(pglist == NULL) goto init_pglist;

    top = pglist->npgs - 1;
    index = (top + bottom) / 2;

    while(strcmp_result = strcmp(hostname, pglist->index[index]->hostname)) {
	if(strcmp_result > 0) {
	    bottom = index + 1;
	}

	else {
	    top = index - 1;
	}

	if(bottom > top) break;
	index = (top + bottom) / 2;
    }

    if(!strcmp_result) goto insert_pid;
    if(strcmp_result > 0) index++;

    goto add_process_group;

init_pglist:
    pglist = malloc(sizeof(process_groups));

    if(pglist) {
	pglist->data		= NULL;
	pglist->index		= NULL;
	pglist->npgs		= 0;
	pglist->npgs_allocated	= 0;
    }

    else {
	goto register_alloc_error;
    }

add_process_group:
    if(pglist->npgs == pglist->npgs_allocated) {
	process_group * pglist_data_backup	= pglist->data;
	process_group ** pglist_index_backup	= pglist->index;
	ptrdiff_t offset;

	pglist->npgs_allocated += increment;

	backup_ptr = pglist->data;
	pglist->data = realloc(pglist->data, sizeof(process_group) *
		pglist->npgs_allocated);

	if(pglist->data == NULL) {
	    pglist->data = backup_ptr;
	    goto register_alloc_error;
	}

	backup_ptr = pglist->index;
	pglist->index = realloc(pglist->index, sizeof(process_group *) *
		pglist->npgs_allocated);

	if(pglist->index == NULL) {
	    pglist->index = backup_ptr;
	    goto register_alloc_error;
	}

	if(offset = (size_t)pglist->data - (size_t)pglist_data_backup) { 
	    for(i = 0; i < pglist->npgs; i++) {
		pglist->index[i] = (process_group *)((size_t)pglist->index[i] +
			offset);
	    }
	}
    }

    for(i = pglist->npgs; i > index; i--) {
	pglist->index[i] = pglist->index[i-1];
    }

    pglist->data[pglist->npgs].hostname		= hostname;
    pglist->data[pglist->npgs].pid		= -1;
    pglist->data[pglist->npgs].plist_indices	= NULL;
    pglist->data[pglist->npgs].npids		= 0;
    pglist->data[pglist->npgs].npids_allocated	= 0;

    pglist->index[index] = &pglist->data[pglist->npgs++];

insert_pid:
    pg = pglist->index[index];

    if(pg->npids == pg->npids_allocated) {
	if(pg->npids_allocated) {
	    pg->npids_allocated <<= 1;

	    if(pg->npids_allocated < pg->npids) pg->npids_allocated = SIZE_MAX;
	    if(pg->npids_allocated > (size_t)nprocs) pg->npids_allocated = nprocs;
	}

	else {
	    pg->npids_allocated = 1;
	}

	backup_ptr = pg->plist_indices;
	pg->plist_indices = realloc(pg->plist_indices, pg->npids_allocated * sizeof(int));

	if(pg->plist_indices == NULL) {
	    pg->plist_indices = backup_ptr;
	    goto register_alloc_error;
	}
    }

    pg->plist_indices[pg->npids++] = plist_index;

    return;

register_alloc_error:
    if(pglist) {
	if(pglist->data) {
	    for(pg = pglist->data; pglist->npgs--; pg++) {
		if(pg->plist_indices) free(pg->plist_indices);
	    }

	    free(pglist->data);
	}

	if(pglist->index) free(pglist->index);

	free(pglist);
    }

    alloc_error = 1;
}

void free_memory(void)
{
    if(pglist) {
	if(pglist->data) {
	    process_group * pg = pglist->data;

	    while(pglist->npgs--) {
		if(pg->plist_indices) free(pg->plist_indices);
		pg++;
	    }

	    free(pglist->data);
	}

	if(pglist->index) free(pglist->index);

	free(pglist);
    }

    if(plist) {
	while(nprocs--) {
	    if(plist[nprocs].device) free(plist[nprocs].device);
	    if(plist[nprocs].hostname) free(plist[nprocs].hostname);
	}

	free(plist);
    }
}

void cleanup(void)
{
    int i;

    if (use_totalview) {
	fprintf(stderr, "Cleaning up all processes ...");
    }
    if (use_totalview)
        MPIR_debug_state = MPIR_DEBUG_ABORTING;
#ifdef MAC_OSX
    for (i = 0; i < NSIG; i++) {
#else
    for (i = 0; i < _NSIG; i++) {
#endif
        signal(i, SIG_DFL);
    }

    if(pglist) {
	rkill_fast();
    }

    else {
	for (i = 0; i < nprocs; i++) {
	    if (RUNNING(i)) {
		/* send terminal interrupt, which will hopefully 
		   propagate to the other side. (not sure what xterm will
		   do here.
		   */
		kill(plist[i].pid, SIGINT);
	    }
	}

	sleep(1);

	for (i = 0; i < nprocs; i++) {
	    if (plist[i].state != P_NOTSTARTED) {
		/* send regular interrupt to rsh */
		kill(plist[i].pid, SIGTERM);
	    }
	}

	sleep(1);

	for (i = 0; i < nprocs; i++) {
	    if (plist[i].state != P_NOTSTARTED) {
		/* Kill the processes */
		kill(plist[i].pid, SIGKILL);
	    }
	}

	rkill_linear();
    }

    exit(EXIT_FAILURE);
}

void rkill_fast(void) {
    int tryagain, spawned_pid[pglist->npgs];
    size_t i, j;

    fprintf(stderr, "Killing remote processes...");

    for(i = 0; i < pglist->npgs; i++) {
	if(0 == (spawned_pid[i] = fork())) {
	    if(pglist->index[i]->npids) {
		const size_t bufsize = 40 + 10 * pglist->index[i]->npids;
		const process_group * pg = pglist->index[i];
		char kill_cmd[bufsize], tmp[10];

		kill_cmd[0] = '\0';

		if(legacy_startup) {
		    strcat(kill_cmd, "kill -s 9");
		    for(j = 0; j < pg->npids; j++) {
			snprintf(tmp, 10, " %d",
				plist[pg->plist_indices[j]].remote_pid);
			strcat(kill_cmd, tmp);
		    }
		}

		else {
		    strcat(kill_cmd, "kill");
		    snprintf(tmp, 10, " %d", pg->pid);
		    strcat(kill_cmd, tmp);
		}

		strcat(kill_cmd, " >&/dev/null");

		if(use_rsh) {
		    execl(RSH_CMD, RSH_CMD, pg->hostname, kill_cmd, NULL);
		}

		else {
		    execl(SSH_CMD, SSH_CMD, SSH_ARG, "-x", pg->hostname,
			    kill_cmd, NULL);
		}

		perror(NULL);
		exit(EXIT_FAILURE);
	    }

	    else {
		exit(EXIT_SUCCESS);
	    }
	}
    }

    while(1) {
	static int iteration = 0;
	tryagain = 0;

	sleep(1 << iteration);

	for (i = 0; i < pglist->npgs; i++) {
	    if(spawned_pid[i]) {
		if(!(spawned_pid[i] = waitpid(spawned_pid[i], NULL, WNOHANG))) {
		    tryagain = 1;
		}
	    }
	}

	if(++iteration == 5 || !tryagain) {
	    fprintf(stderr, "DONE\n");
	    break;
	}
    }

    if(tryagain) {
	fprintf(stderr, "The following processes may have not been killed:\n");
	for (i = 0; i < pglist->npgs; i++) {
	    if(spawned_pid[i]) {
		const process_group * pg = pglist->index[i];

		fprintf(stderr, "%s:", pg->hostname);

		for (j = 0; j < pg->npids; j++) {
		    fprintf(stderr, " %d", plist[pg->plist_indices[j]].remote_pid);
		}

		fprintf(stderr, "\n");
	    }
	}
    }
}

void rkill_linear(void) {
    int i, tryagain, spawned_pid[nprocs];

    fprintf(stderr, "Killing remote processes...");

    for (i = 0; i < nprocs; i++) {
	if(0 == (spawned_pid[i] = fork())) {
	    char kill_cmd[80];

	    if(!plist[i].remote_pid) exit(EXIT_SUCCESS);

	    snprintf(kill_cmd, 80, "kill -s 9 %d >&/dev/null",
		plist[i].remote_pid);

	    if(use_rsh) {
		execl(RSH_CMD, RSH_CMD, plist[i].hostname, kill_cmd, NULL);
	    }

	    else {
		execl(SSH_CMD, SSH_CMD, SSH_ARG, "-x",
			plist[i].hostname, kill_cmd, NULL);
	    }

	    perror(NULL);
	    exit(EXIT_FAILURE);
	}
    }

    while(1) {
	static int iteration = 0;
	tryagain = 0;

	sleep(1 << iteration);

	for (i = 0; i < nprocs; i++) {
	    if(spawned_pid[i]) {
		if(!(spawned_pid[i] = waitpid(spawned_pid[i], NULL, WNOHANG))) {
		    tryagain = 1;
		}
	    }
	}

	if(++iteration == 5 || !tryagain) {
	    fprintf(stderr, "DONE\n");
	    break;
	}
    }

    if(tryagain) {
	fprintf(stderr, "The following processes may have not been killed:\n");
	for (i = 0; i < nprocs; i++) {
	    if(spawned_pid[i]) {
		fprintf(stderr, "%s [%d]\n", plist[i].hostname,
			plist[i].remote_pid);
	    }
	}
    }
}

int file_exists (char *filename) 
{
    FILE *fp = fopen (filename, "r");
    if (fp) {
        fclose (fp);
        return 1;
    }
    return 0;
}

int getpath(char *buf, int buf_len)
{
    char link[32];
    pid_t pid;
    unsigned len;
    pid = getpid();
    snprintf(&link[0], sizeof(link), "/proc/%i/exe", pid);

    if ((len = readlink(&link[0], buf, buf_len)) == -1) {
        buf[0] = 0;
        return 0;
    }
    else
    {
        buf[len] = 0;
        while (len && buf[--len] != '/');
        if (buf[len] == '/') buf[len] = 0;
        return len;
    }
}

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define CHECK_ALLOC() do { \
    if (tmp) { \
        free (mpispawn_env); \
        mpispawn_env = tmp; \
    } \
    else goto allocation_error; \
} while (0);

void spawn_fast(int argc, char *argv[], char *totalview_cmd, char *env) {


    char * mpispawn_env, * tmp, * ld_library_path;
    char * name, * value;
    int i, n;
    char pathbuf[PATH_MAX];
    int pathlen;

    if(ld_library_path = getenv("LD_LIBRARY_PATH")) {
	mpispawn_env = mkstr("MPISPAWN_LD_LIBRARY_PATH=%s:%s",
		LD_LIBRARY_PATH_MPI, ld_library_path);
    }

    else {
    	mpispawn_env = mkstr("MPISPAWN_LD_LIBRARY_PATH=%s", LD_LIBRARY_PATH_MPI);
    }

    if(!mpispawn_env) goto allocation_error;

    tmp = mkstr("%s MPISPAWN_MPIRUN_MPD=0", mpispawn_env);
    CHECK_ALLOC ();

    tmp = mkstr("%s MPISPAWN_MPIRUN_HOST=%s", mpispawn_env, mpirun_host);
    CHECK_ALLOC ();

    tmp = mkstr("%s MPISPAWN_CHECKIN_PORT=%d", mpispawn_env, port);
    CHECK_ALLOC ();
    
    tmp = mkstr("%s MPISPAWN_MPIRUN_PORT=%d", mpispawn_env, port);
    CHECK_ALLOC ();

    tmp = mkstr("%s MPISPAWN_GLOBAL_NPROCS=%d", mpispawn_env, nprocs);
    CHECK_ALLOC ();

    tmp = mkstr("%s MPISPAWN_MPIRUN_ID=%d", mpispawn_env, getpid());
    CHECK_ALLOC ();

    if (use_totalview) {
        tmp = mkstr("%s MPISPAWN_USE_TOTALVIEW=1", mpispawn_env);
        CHECK_ALLOC ();
        

    }

    /* 
     * mpirun_rsh allows env variables to be set on the commandline
     */
    if(!mpispawn_param_env) {
	mpispawn_param_env = mkstr("");
	if(!mpispawn_param_env) goto allocation_error;
    }

	while(aout_index != argc && strchr(argv[aout_index], '=')) {
		name = strdup(argv[aout_index++]);
		value = strchr(name, '=');
		value[0] = '\0';
		value++;

		tmp = mkstr("%s MPISPAWN_GENERIC_NAME_%d=%s"
			" MPISPAWN_GENERIC_VALUE_%d=%s", mpispawn_param_env,
			param_count, name, param_count, value);

		free(name);
		free(mpispawn_param_env);

		if(tmp) {
			mpispawn_param_env = tmp;
			param_count++;
		}

		else {
			goto allocation_error;
		}
	}

    if (aout_index == argc) {
        fprintf(stderr, "Incorrect number of arguments.\n");
        usage();
        exit (EXIT_FAILURE);
    }

    i = argc - aout_index;
    if(debug_on && !use_totalview) i++;

    tmp = mkstr("%s MPISPAWN_ARGC=%d", mpispawn_env, i);
    CHECK_ALLOC ();
    
    i = 0;

    if (debug_on && !use_totalview) {
	tmp = mkstr("%s MPISPAWN_ARGV_%d=%s", mpispawn_env, i++, DEBUGGER);

    CHECK_ALLOC ();
    }
    
    if(use_totalview) {
        int j;
        for (j = 0; j < MPIR_proctable_size; j++) {
            MPIR_proctable[j].executable_name = argv[aout_index];
        }
    }

    while(aout_index < argc) {
        tmp = mkstr("%s MPISPAWN_ARGV_%d=%s", mpispawn_env, i++,
            argv[aout_index++]);
        CHECK_ALLOC ();
    }

    if(mpispawn_param_env) {
	tmp = mkstr("%s MPISPAWN_GENERIC_ENV_COUNT=%d %s", mpispawn_env,
		param_count, mpispawn_param_env);

	free(mpispawn_param_env);
	free(mpispawn_env);

	if(tmp) {
	    mpispawn_env = tmp;
	}

	else {
	    goto allocation_error;
	}
    }

    for(i = 0; i < pglist->npgs; i++) {
	if(!(pglist->data[i].pid = fork())) {
	    size_t arg_offset = 0;
	    const char* argv[7];
	    char *command;

	    tmp = mkstr("%s MPISPAWN_ID=%d", mpispawn_env, i);
        CHECK_ALLOC ();

	    tmp = mkstr("%s MPISPAWN_LOCAL_NPROCS=%d", mpispawn_env,
		    pglist->data[i].npids);
        CHECK_ALLOC ();

	    tmp = mkstr("%s MPISPAWN_WORKING_DIR=%s", mpispawn_env, wd);
        CHECK_ALLOC ();

	    for(n = 0; n < pglist->data[i].npids; n++) {
		tmp = mkstr("%s MPISPAWN_MPIRUN_RANK_%d=%d", mpispawn_env, n,
			pglist->data[i].plist_indices[n]);
        CHECK_ALLOC ();

		if(plist[pglist->data[i].plist_indices[n]].device != NULL) {
		    tmp = mkstr("%s MPISPAWN_VIADEV_DEVICE_%d=%s", mpispawn_env,
			    n,
			    plist[pglist->data[i].plist_indices[n]].device);
            CHECK_ALLOC ();
		}

		tmp = mkstr("%s MPISPAWN_VIADEV_DEFAULT_PORT_%d=%d",
			mpispawn_env, n,
			plist[pglist->data[i].plist_indices[n]].port);
        CHECK_ALLOC ();
	    }

	    if(xterm_on) {
		argv[arg_offset++] = XTERM;
		argv[arg_offset++] = "-e";
	    }

	    if(use_rsh) {
		argv[arg_offset++] = RSH_CMD;
	    }

	    else {
		argv[arg_offset++] = SSH_CMD;
		argv[arg_offset++] = SSH_ARG;
	    }
        
        if (getpath(pathbuf, PATH_MAX) && file_exists (pathbuf)) {
    	    command = mkstr("cd %s; %s %s %s %s/mpispawn", wd, ENV_CMD,
    		    mpispawn_env, env, pathbuf);
        }
        else if (use_dirname) {
    	    command = mkstr("cd %s; %s %s %s %s/mpispawn", wd, ENV_CMD,
    		    mpispawn_env, env, binary_dirname);
        }
        else {
    	    command = mkstr("cd %s; %s %s %s mpispawn", wd, ENV_CMD,
    	        mpispawn_env, env);
        }

	    if(!command) {
		fprintf(stderr, "Couldn't allocate string for remote command!\n");
		exit(EXIT_FAILURE);
	    }

	    argv[arg_offset++] = pglist->data[i].hostname;
	    argv[arg_offset++] = command;
	    argv[arg_offset++] = NULL;

	    if(show_on) {
		size_t arg = 0;
		fprintf(stdout, "\n");
		while(argv[arg] != NULL) fprintf(stdout, "%s ", argv[arg++]);
		fprintf(stdout, "\n");

		exit(EXIT_SUCCESS);
	    }

	    if(strcmp(pglist->data[i].hostname, plist[0].hostname)) {
               int fd = open("/dev/null", O_RDWR, 0);
               dup2(fd, STDIN_FILENO);
	    }


	    execv(argv[0], (char* const*) argv);
	    perror("execv");

	    for(i = 0; i < argc; i++) {
		fprintf(stderr, "%s ", argv[i]);
	    }

	    fprintf(stderr, "\n");

	    exit(EXIT_FAILURE);
	}
    }

    return;

allocation_error:
    perror("spawn_fast");
    if(mpispawn_env) {
	fprintf(stderr, "%s\n", mpispawn_env);
	free(mpispawn_env);
    }

    exit(EXIT_FAILURE);
}

#undef CHECK_ALLOC
void spawn_linear(int argc, char *argv[], char *totalview_cmd, char *env)
{
    char command_name[COMMAND_LEN];
    char command_name_tv[COMMAND_LEN];
    int i;

    make_command_strings(argc, argv, totalview_cmd, command_name, command_name_tv);

    /* start all processes */
    for (i = 0; i < nprocs; i++) {
	if((use_totalview) && (i == 0)) {
	    if (start_process(i, command_name_tv, env) < 0) {
		fprintf(stderr, 
			"Unable to start process %d on %s. Aborting.\n", 
			i, plist[i].hostname);
		cleanup();
	    }
	} else {
	    if (start_process(i, command_name, env) < 0) {
		fprintf(stderr, 
			"Unable to start process %d on %s. Aborting.\n", 
			i, plist[i].hostname);
		cleanup(); 
	    } 
	}
    }
}

void make_command_strings(int argc, char *argv[], char *totalview_cmd, char * command_name, char * command_name_tv) 
{


    int i;
    if (debug_on) {
    fprintf (stderr,"debug enabled !\n");
	char keyval_list[COMMAND_LEN];
	sprintf(keyval_list, "%s", " ");
	/* Take more env variables if present */
	while (strchr(argv[aout_index], '=')) {
	    strcat(keyval_list, argv[aout_index]);
	    strcat(keyval_list, " ");
	    aout_index ++;
	}
	if(use_totalview) {
	    sprintf(command_name_tv, "%s %s %s", keyval_list, 
		    totalview_cmd, argv[aout_index]);
	    sprintf(command_name, "%s %s ", keyval_list, argv[aout_index]);
	} else {
	    sprintf(command_name, "%s %s %s", keyval_list, 
		    DEBUGGER, argv[aout_index]);
	}
    } else {
	sprintf(command_name, "%s", argv[aout_index]);
    }

    if(use_totalview) {
	/* Only needed for root */
	strcat(command_name_tv, " -a ");
    }

    /* add the arguments */
    for (i = aout_index + 1; i < argc; i++) {
    	strcat(command_name, " ");
    	strcat(command_name, argv[i]);
    }
    /* */


    if(use_totalview) {
	/* Complete the command for non-root processes */
	strcat(command_name, " -mpichtv");

	/* Complete the command for root process */
	for (i = aout_index + 1; i < argc; i++) {
	    strcat(command_name_tv, " ");
	    strcat(command_name_tv, argv[i]);
	}
	strcat(command_name_tv, " -mpichtv");
    }

}

void nostop_handler(int signal)
{
    printf("Stopping from the terminal not allowed\n");
}

void alarm_handler(int signal)
{
    extern const char * alarm_msg;

    if (use_totalview) {
	fprintf(stderr, "Timeout alarm signaled\n");
    }

    if(alarm_msg) fprintf(stderr, alarm_msg);

    fprintf(stderr, "ERROR: Reached mpirun timeout.  Attempting to cleanup job.\n"
            "If this job is not an MPI application, you may want to run it\n"
            "directly (without mpirun) or via \"srun --mpi=none\", if available.\n"
    );

    cleanup();
}


void child_handler(int signal)
{
    static int num_exited = 0;
    int status, pid, num_children = nprocs;

    if(pglist && !legacy_startup) num_children = pglist->npgs;

    while(1) {
	pid = waitpid(-1, &status, WNOHANG);
	if(pid == 0) break;

	if(pid != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
	    if(++num_exited == num_children) {
            if (legacy_startup)
                close(server_socket);
		exit(WEXITSTATUS(status));
	    }
	}

	else {
	    fprintf(stderr, "\nChild exited abnormally!\n");
	    cleanup();
	}
    }
}

void mpispawn_checkin(int s, struct sockaddr *sockaddr, unsigned int
	sockaddr_len)
{
    int sock, id, i, n, mpispawn_root;
    in_port_t port;
    socklen_t addrlen;
    struct sockaddr_storage addr, address[pglist->npgs];
    int mt_degree;
    mt_degree = env2int ("MT_DEGREE");
    if (!mt_degree) {
        mt_degree = ceil (pow (pglist->npgs, (1.0/(MT_MAX_LEVEL - 1))));
        if (mt_degree < MT_MIN_DEGREE)
            mt_degree = MT_MIN_DEGREE;
        if (mt_degree > MT_MAX_DEGREE)
            mt_degree = MT_MAX_DEGREE;
    }
    else {
        if (mt_degree < 2) {
            fprintf (stderr, "mpirun_rsh: MT_DEGREE too low");
            cleanup ();
        }
    }

    for(i = 0; i < pglist->npgs; i++) {
	    addrlen = sizeof(addr);

	    while ((sock = accept(s, (struct sockaddr *)&addr, &addrlen)) < 0) {
	        if (errno == EINTR || errno == EAGAIN) continue;

	        perror ("accept [mpispawn_checkin]");
	        cleanup();
	    }

	    if (read_socket(sock, &id, sizeof(int))
	    	    || read_socket(sock, &pglist->data[id].pid, sizeof(pid_t))
	    	    || read_socket(sock, &port, sizeof(in_port_t))) {
	        cleanup();
	    }

	    address[id] = addr;
	    ((struct sockaddr_in *)&address[id])->sin_port = port;

	    if (!(id == 0 && use_totalview))
            close(sock);
        else 
            mpispawn_root = sock;

	    for (n = 0; n < pglist->data[id].npids; n++) {
	        plist[pglist->data[id].plist_indices[n]].state = P_STARTED;
	    }
    }

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (sock < 0) {
    	perror("socket [mpispawn_checkin]");
	    cleanup();
    }

    if (connect(sock, (struct sockaddr *) &address[0],
		    (socklen_t)sizeof(struct sockaddr)) < 0) {
	    perror("connect");
	    cleanup();
    }

    /*
     * Send address array to address[0] (mpispawn with id 0).  The mpispawn
     * processes will propagate this information to each other after connecting
     * in a tree like structure.
     */
    if (write_socket(sock, &pglist->npgs, sizeof(pglist->npgs))
	        || write_socket(sock, &address, sizeof(addr) * pglist->npgs)
            || write_socket (sock, &mt_degree, sizeof (int))) {
	    cleanup();
    }

    close(sock);

    if (use_totalview) {
        int id, j; 
        process_info_t *pinfo = (process_info_t *) malloc 
                (process_info_s * nprocs);
        read_socket (mpispawn_root, pinfo, process_info_s * nprocs);
        for (j = 0; j < nprocs; j++) {
            MPIR_proctable[pinfo[j].rank].pid = pinfo[j].pid; 
        }
        free (pinfo);
        /* We're ready for totalview */
        MPIR_debug_state = MPIR_DEBUG_SPAWNED;
        MPIR_Breakpoint ();

        /* MPI processes can proceed now */
        id = 0;
        write_socket (mpispawn_root, &id, sizeof (int));
        close (mpispawn_root);
    }
}

/* vi:set sw=4 sts=4 tw=80: */
