/**
Minidaemon ADT Implementation, provided by Mellanox, MPI Team
mailto : xalex@mellanox.co.il
**/
#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <netinet/in.h>
#include <errno.h>
#include <netdb.h>
#include <string.h>
#ifdef MAC_OSX
#include <sys/wait.h>
#else
#include <wait.h>
#endif
#include <assert.h>
#include <fcntl.h>
#include <time.h>



#include "minidaemon.h"

#define RSH_CMD	"/usr/bin/rsh"
#define SSH_CMD	"/usr/bin/ssh"
#define SSH_ARG "-q"

#define ENV_CMD "/usr/bin/env"

#ifndef LD_LIBRARY_PATH_MPI
#define LD_LIBRARY_PATH_MPI "/usr/mvapich/lib/shared"
#endif

#ifndef MPI_PREFIX
#define MPI_PREFIX /usr/mvapich/
#endif

#define MPI_LIB lib/shared/
#define MPI_BIN bin/
#define MINIDAEMON_CLIENT_NAME minidaemon_client

/*#define MPI_LD_LIBRARY_PATH MPI_PREFIX##MPI_LIB
#define MPI_BIN_PATH MPI_PREFIX##MPI_BIN
#define MINIDAEMON_EXEC_PATH MPI_BIN_PATH##MINIDAEMON_CLIENT_NAME*/
#define STR_CONCAT(a,b,c)  a ## b ## c
#define STR_CONCAT2(a,b)  a ## b 

#define STR(s) #s
#define XSTR(s) STR(s)
#define MPI_BIN_PATH

#define MAX_WD_LEN 256
#define MAX_HOST_LEN 256

#define MAX_MESSAGE_SIZE 512
#define MINIDAEMON_PORT 4000
#define ACCEPT_TIMEOUT 100
#define NO_TIMEOUT 1000

#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))



/* #define CH_LIST_FOREACH_MD_NODES  */

/* Minidaemon basic types definition */
typedef enum {CH_JOB_EXT = -1,P_OK = 0, P_KILLED, STATUS_REQUEST, STATUS , KILL} MD_message;
typedef enum {MD_INIT = 0, MD_QP_LID, MD_WORK, MD_TERM } MD_phase;
typedef enum {MD_FINISH = 0, MD_NORMAL, MD_UNKNOWN, MD_NOT_STARTED} MD_status; /* TODO MD_RUN instead of NORMAL */

/*Timeout values for WORK stage */
static const int send2father_work_timeout = 10;
static const int recv_parent_work_timeout = 31;
static const int recv_children_work_timeout = 31;
static const int recv_jobs_work_timeout = NO_TIMEOUT;

/* Timeout values for TERM stage */
static const int send2father_term_timeout = NO_TIMEOUT;
static const int recv_parent_term_timeout = NO_TIMEOUT;
static const int recv_children_term_timeout = 30;
static const int recv_jobs_term_timeout = 10;


/** These 2 structures are for temporarily testing only **/
/**-----------------------------------------------------------**/

struct minidaemon_t {
	/* private data members */
	const char	*parent_name;
	int		tree_width; /* md_num */
	int		ch_num; /* total process number, i.e the length of children_md_arr */
	int		root_ch_num;
	
	MD_phase	phase; /* phase of execution */
	
	MD_entry	children_md_arr; /* The list of children and parent minidaemon process and own jobs */
	
	ChildrenList childrenList; /* The list of parameters for minidaemon itself and its subtree */
	
	/*********** PUBLIC METHODS *******************/
	
	/* TODO this methods would be implemented in xlauncher ver 2.0 */
	/*void 		(*timeout_handler)();
	void 		(*data_handler)();*/
	
	int		total_running_jobs;
	int		total_running_mds;
	int		curr_running_jobs;
	int		curr_running_mds;
	
	/* timeout variables */
	int 		send2father_timeout ;
	int 		recv_parent_timeout ;
	int 		recv_children_timeout ;
	int 		recv_jobs_timeout ;
	
	int         mpi_num_of_params; /* number of mpi parametres that we pass to mpilib */
    char  *mpi_params;       /* mpi parametres VIADEV_.... */
    char  *wd;               /* current work directory */
    char  *command;
    char  *remote_sh_command;
    char  *remote_sh_args;
	char 	*root_hostname;
	int		mpi_param_len;
	
	int 		max_fd_num;
	
	int		isParent;
	int 	parent_fd;
	
	int 	pid;
	int 	mpirun_port;
	int 	ppid;
	int 	isLeaf;
	int 	array_it;

	md_exit_status_type md_exit_value;

	
};
static struct minidaemon_t md_entity;

struct md_entry_t {
	int fd;
	MD_status stat;
	struct sockaddr * sock_addr;
	char	resp_stat;
};

struct childrenList_t {
    char *hostname;
    char *device;
    int pid; 
    int port;
	int rank;
	
	int proc_state;
	int child_list_len; /* Only for MD nodes */
};

int resp_status_def = 1;
const int MD_do_not_set = -2;
const int attempt_no = 10;

typedef int ChildrenListIterator;

/* TODO all functions that use 'private' data member Minidaemon should be static */
static int 					children_list_comp (const void *p1, const void *p2);
static ChildrenListIterator 	getListNextNode(ChildrenListIterator it, int offset);

static void 	md_socket_create();
void 			md_connect_to_parent();
void 			md_listen_parent(int sd);
void 			md_general_listen();
void 			md_socket_close();
void            md_base_init(int ch_num, int width, const char * remote_sh_command, 
                              const char * remote_sh_args, int mpirun_port, int ppid, 
                              const char * root_host, int root_ch_num, char * wd, 
                              int num_of_params, const char * mpi_params, const char * command); 
void 			md_start_node_tree();
int 			md_start_node(ChildrenListIterator i);
void 			md_run_own_jobs();

void 			md_children_proc_handler(int ind,int time_delta);
void 			md_handler(int sd, int i);
void 			md_status_msg_handler(const int * msg_buf,int i, int size);
void 			md_req_status_handler(const char * buff);

void 			md_send_status_message(int md_index, ChildrenListIterator iter);
void 			md_send_init_message(int fd);
void 			md_rebuild_mask(fd_set * mask);
int 			md_update_socket_status(int ind,int time_delta);

void 			md_start_termination_process();
void 			md_forced_cleanup_handler();
void 			md_send_termination_message(int sd);

void 			md_print_status_message();
void 			md_read_int(int * buff, int sd);
int				md_read_gen_type(void * buff,size_t len,int sd, int att_no);
void 			md_read_string(char ** buff, int sd);
void 			md_build_command_string(int i);
void 			md_list_print();
static void 	get_display_str(char * display);
static void     setenv_mpi_params();
static char**   str_to_argv(const char *str);
static char*    add_word_to_string(char *str, const char *elem );

void 			timeout_handler (int delta_time);
void			sigpipe_handler(int signo);

static int children_list_comp (const void *p1, const void *p2) {
	ChildrenList proc1 = (ChildrenList ) p1;
	ChildrenList proc2 = (ChildrenList ) p2;
	return strcmp(proc1->hostname, proc2->hostname); /* i.e, 2 string are equal iff strcmp==0 */
}

static ChildrenListIterator getListNextNode(ChildrenListIterator it, int offset) {
	ChildrenListIterator it_next = it + 1;
	
	if (it < 0 || offset < 0) {
		MD_USR_ERROR("<getListNextNode> Bad Iterator received");
	}
	
	if (it_next >= md_entity.ch_num )
		return md_entity.ch_num;
	
	while (offset > 0 && it_next < md_entity.ch_num) {
		while (children_list_comp( &md_entity.childrenList[it], &md_entity.childrenList[it_next]) == 0) {
			if ( (++it_next >= md_entity.ch_num) )
				return md_entity.ch_num; /* return there maximum iterator value +1 in order to notify list end */
		}
		--offset;
		it=it_next;
	}
	return it_next; /*the actual index or the end of the list (ch_num) should be returned */
}

void listPrint() {
	int i, j=0;
	MD_PRINT(DDEBUG,"Node %d:",j);
	for (i = 0 ; i < md_entity.ch_num; ++i) {
		MD_PRINT(DDEBUG," %s",md_entity.childrenList[i].hostname);
		if (children_list_comp( &md_entity.childrenList[i], &md_entity.childrenList[j]) != 0) {
			j=i;
			MD_PRINT(DDEBUG,"\nNode %d", j);
		}
	}	
	/* fprintf(stderr,"\n"); */
}

/**
Create a root instance of MiniDaemon
This function should be
**/
void minidaemon_create(process * procList, int nproc, int width, int mpirun_port, int ppid,
        const char * remote_sh_command, const char * remote_sh_args, char * wd, int num_of_params,
        const char * mpi_params, const char * command) {
	int i;
	MD_PRINT(DDEBUG,"<minidaemon_create> Starting root minidaemon with following parameters:\n");
	MD_PRINT(DDEBUG,"nproc = %d, width = %d, command = %s, mpi_params = %s\n",
		nproc, width, command, mpi_params);
    md_base_init(nproc, width, remote_sh_command, remote_sh_args, 
            mpirun_port, ppid, NULL, nproc, wd, num_of_params, mpi_params, command);
	for (i = 0; i < nproc; ++i) {
		/* TODO memcpy(&md_entity.childrenList[i], &procList[i], sizeof(procList[i].hostname)+sizeof(procList[i].device) + sizeof(procList[i].port)); */
		md_entity.childrenList[i].hostname = 	procList[i].hostname;
		md_entity.childrenList[i].device = 		procList[i].device;
		md_entity.childrenList[i].port = 		procList[i].port;
		md_entity.childrenList[i].rank = 		i;
		md_entity.childrenList[i].proc_state = 	MD_NOT_STARTED;
	}
	
	listPrint();
	qsort(md_entity.childrenList, nproc, sizeof(struct childrenList_t), children_list_comp);
	listPrint();
	md_entity.isParent	 = 1;
	if ( !(md_entity.root_hostname = (char *) malloc(MAX_HOST_LEN*sizeof(char)))) {
		MD_SYS_ERROR("<minidaemon_create> : malloc");
	}
	if (gethostname(md_entity.root_hostname,MAX_HOST_LEN) == -1) {
		MD_SYS_ERROR("<minidaemon_create> : gethostname");
	};
	MD_PRINT(DDEBUG,"<minidaemon_create> md_entity.root_hostname is %s\n",md_entity.root_hostname);
}

/* Init all data member structures */
void minidaemon_init ( const char * par_name , int ch_num, int width,  const char * remote_sh_command, 
        const char * remote_sh_args, const char * root_host , int root_ch_num, int mpirun_port, 
        int ppid, int array_it, char * wd, int num_params, const char * mpi_params, const char * command) 
{

	MD_PRINT(DDEBUG,"<minidaemon_init> Starting minidaemon with following parameters:\n");
    MD_PRINT(DDEBUG,
            "parent_name=%s, ch_num=%d, width=%d, mpi_params=%s\ncommand=%s, root_host=%s, root_ch_num=%d, mpirun_port=%d, wd=%s, num_params=%d\n", 
            par_name, ch_num, width, mpi_params, command, root_host, root_ch_num, mpirun_port, wd, num_params);
		
    md_base_init(ch_num, width, remote_sh_command, remote_sh_args,
            mpirun_port, ppid, root_host, root_ch_num,  wd, num_params, mpi_params, command);
	
	md_entity.parent_name 		= par_name;
	md_entity.isParent = 0;
	md_entity.array_it = array_it;
	md_entity.pid = getpid();

}

void md_base_init(int ch_num, int width, const char * remote_sh_command, const char * remote_sh_args,
        int mpirun_port, int ppid, const char * root_host, int root_ch_num, char * wd, 
        int num_of_params, const char * mpi_params, const char * command) 
{
	if ( !(md_entity.children_md_arr = malloc((width+ch_num+1) * sizeof(struct md_entry_t))) ||
		 !(md_entity.childrenList = malloc(ch_num * sizeof(struct childrenList_t))) ) {
		MD_SYS_ERROR("<minidaemon_init> malloc failed");
	} 

	md_entity.ch_num 			= ch_num;
	md_entity.root_ch_num		= root_ch_num;
	md_entity.root_hostname		= (char * ) root_host;
	md_entity.tree_width 		= width;
	
    md_entity.mpi_num_of_params  = num_of_params;
    md_entity.mpi_params         = strdup(mpi_params);
	md_entity.mpi_param_len	     = strlen(mpi_params);
    md_entity.wd                 = strdup(wd);
    md_entity.command            = strdup(command);
	md_entity.remote_sh_command  = strdup(remote_sh_command);
	md_entity.remote_sh_args 	 = strdup(remote_sh_args);
	
	md_entity.max_fd_num = 0;
	md_entity.total_running_mds = 0;
	md_entity.phase = MD_INIT;
	md_entity.mpirun_port = mpirun_port;
	md_entity.ppid = ppid;
	md_entity.isLeaf = 0;
	md_entity.md_exit_value = MD_EXIT_NORMAL;
}

/* create parent socket and children minidaemon sockets */
static void md_socket_create() {
	
	struct hostent* host;
	struct utsname name;
	struct sockaddr_in srv_sock_name,clnt_sock_name;
	int s;
	long arg;
	
	md_entity.phase = MD_WORK;
	if (md_entity.total_running_mds < 1) 
		return;
		
	if ((s = socket (AF_INET, SOCK_STREAM, 0)) < 0) {
		perror ("socket");
		exit(MD_EXIT_SYS_ERROR);
	}
	
	
	memset ((char *)&srv_sock_name, '\0', sizeof(srv_sock_name));
	srv_sock_name.sin_port = htons (MINIDAEMON_PORT);
	srv_sock_name.sin_family = AF_INET;
	
	/* determine host system name and internet address*/
	if (uname(&name) == -1) {
		perror ("uname");
		exit(MD_EXIT_SYS_ERROR);
	}
	
	if ( (host=gethostbyname(name.nodename)) == NULL) {
		MD_PRINT(DDEBUG,"The problem is here, nodename is %s\n",name.nodename);
		perror ("gethostbyname");
		exit(MD_EXIT_SYS_ERROR);
	} 
	
	/*memcpy ( (char*) &srv_sock_name.sin_addr, host->h_addr_list, host->h_length);*/
	srv_sock_name.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind (s, (struct sockaddr *) &srv_sock_name, sizeof (srv_sock_name)) < 0) {
		perror ("bind");
		close (s);
		exit(MD_EXIT_SYS_ERROR);
	}
	
	/* Set non-blocking */
	if( (arg = fcntl(s, F_GETFL, NULL)) == 0) { 
		fprintf(stderr, "Error fcntl(..., F_GETFL) (%s)\n", strerror(errno)); 
		exit(MD_EXIT_SYS_ERROR); 
	} 
	arg |= O_NONBLOCK; 
	if( fcntl(s, F_SETFL, arg) != 0) { 
		fprintf(stderr, "Error fcntl(..., F_SETFL) (%s)\n", strerror(errno)); 
		exit(MD_EXIT_SYS_ERROR); 
	}
	
	/* prepare to listen cp for multiple connections*/
	if (listen (s, md_entity.tree_width) ==-1 ) {
		perror ("listen");
		close (s);
		exit(MD_EXIT_SYS_ERROR);
	}
	
	MD_PRINT(DDEBUG,"Waiting for children to connect...\n");
	int loop = md_entity.total_running_jobs +1 ; 
	
	/* TODO: The following block should be spinned-of as a private method (function) */
	struct timeval timeout;
	timeout.tv_sec = ACCEPT_TIMEOUT;
	timeout.tv_usec = 0;
	int res;
	fd_set mask;
	FD_ZERO(&mask);
	FD_SET(s,&mask);
	socklen_t client_len = (socklen_t) sizeof(clnt_sock_name);
	MD_PRINT(DDEBUG,"<md_socket_create> total_running_mds = %d\n",md_entity.total_running_mds);
	while (loop < md_entity.total_running_mds+ md_entity.total_running_jobs +1) {
		switch (res = select(s+1, &mask, NULL, NULL, &timeout)) {
			case -1:
				/*signal or error were received*/
				/*perror ("select");
				close (s);
				exit(1);*/
			
			case 0 :
				/* timeout was reached */
				MD_PRINT(DDEBUG,"Timeout problem when connecting to children minidaemons");
				md_forced_cleanup_handler();
				exit(MD_EXIT_MINIDAEMON_FAIL);
			default:
				if (res < 0) {
					perror ("Bad select result\n"); 
					exit(MD_EXIT_SYS_ERROR);
				}
				if (FD_ISSET (s, &mask)) { 
					/* TODO : yet another try */
					/*	There may not always be a connection waiting after a SIGIO is delivered or select(2) or poll(2)
					 return a readability event because the  connection might  have  been  removed  by  an asynchronous
					 network error or another thread before accept is called.  If this happens then the call will block
					 waiting for the next connection to arrive.  To ensure that accept never blocks, the passed socket 
					 s needs to have the  O_NONBLOCK  flag  set
					*/
					if ( (md_entity.children_md_arr[loop].fd = 
						accept(s, (struct sockaddr *) &clnt_sock_name, &client_len)) == -1 ) {
						perror ("listen");
						close (s);
						exit(MD_EXIT_SYS_ERROR);
					}
					md_entity.max_fd_num =
					 ( md_entity.max_fd_num > md_entity.children_md_arr[loop].fd 
					 ? md_entity.max_fd_num : md_entity.children_md_arr[loop].fd) ;
					 MD_PRINT(DDEBUG,"Another one had connected to parent\n");
					md_send_init_message(md_entity.children_md_arr[loop].fd);
					/*children_md_arr[loop].sock_addr = clnt_sock_name;*/ /* TODO ? */
					++loop;
				}
				break;
		}
		/* TODO real update socket status with time delta */
		timeout.tv_sec = ACCEPT_TIMEOUT;
		timeout.tv_usec = 0;
		FD_ZERO(&mask);
		FD_SET(s,&mask);
	}
	return;	
}

/* Start listening to messages from other Minidaemons */
void minidaemon_run () {

	ChildrenListIterator i;
	int node_number = 0;
	if (!md_entity.isParent) {
		md_connect_to_parent();
	}
	md_start_node_tree();
	md_run_own_jobs();
	md_socket_create();
	
	/* Here are 4 different stages :
	1. - INIT STATE : Get parameters from parent minidaemon (this stage should be done in md_connect_to_parent()
	2. - QP_LID STAGE : Listen to and handle QP_LID messages in order to run own jobs and pass params to children minidaemons
	3. - WORK STAGE : Listen to STATUS messages from other MD's and keep I-AM-ALIVE on
	4. - TERMINATION STAGE : Listen to messages in order to verify clear shutdown
	*/
	
	md_general_listen();
	MD_PRINT(DDEBUG,"Exiting <md_general_listen> : unhandled signal, pid=%d\n",md_entity.pid);
	if (md_entity.isParent) {
		md_print_status_message();
	}
	exit(MD_EXIT_MINIDAEMON_SIG);
}

void md_connect_to_parent() {
	struct hostent* host;
	struct utsname name;
	struct sockaddr_in srv_sock_name,clnt_sock_name;
	int s;
	long arg;
	int len;
	int i = 0;
	
	MD_PRINT(DDEBUG,"Connecting to parent : %s\n", md_entity.parent_name);
	if ((s = socket (AF_INET, SOCK_STREAM, 0)) < 0) {
		perror ("socket");
		exit(MD_EXIT_SYS_ERROR);
	}
	
	memset ((char *)&srv_sock_name, '\0', sizeof(srv_sock_name));
	srv_sock_name.sin_port = htons (MINIDAEMON_PORT);
	srv_sock_name.sin_family = AF_INET;
	
	if ( (host=gethostbyname(md_entity.parent_name)) == NULL) {
		perror ("gethostbyname");
		exit(MD_EXIT_SYS_ERROR);
	} 
	
	/* TODO host->h_addr_list */
	memcpy ( (char*) &srv_sock_name.sin_addr, host->h_addr, host->h_length);
	
	
	if (connect (s, (struct sockaddr *) &srv_sock_name, sizeof (srv_sock_name)) < 0) {
		perror ("connect");
		close (s);
		exit(MD_EXIT_SYS_ERROR);
	}
	/* Set non-blocking  */
	if( (arg = fcntl(s, F_GETFL, NULL)) == 0) { 
		fprintf(stderr, "Error fcntl(..., F_GETFL) (%s)\n", strerror(errno)); 
		exit(MD_EXIT_SYS_ERROR); 
	} 
	arg |= O_NONBLOCK; 
	if( fcntl(s, F_SETFL, arg) != 0) { 
		fprintf(stderr, "Error fcntl(..., F_SETFL) (%s)\n", strerror(errno)); 
		exit(MD_EXIT_SYS_ERROR); 
	} 
	
	MD_PRINT(DDEBUG,"Connected to parent\n");
	md_entity.max_fd_num = ( md_entity.max_fd_num > s ? md_entity.max_fd_num : s);
	
	/* 
	 Here we not need the communication to be non-blocking 
	 Minidaemon should receive data that is essential for future flow.
	 That is, without information about own processes and children minidaemon it can't continue
	*/
	md_entity.parent_fd = (md_entity.isParent ? 0 : s);
	md_listen_parent(s);
	MD_PRINT(DDEBUG,"Children List received from the parent\n");
}

void md_listen_parent(int sd) {
	int i = 0;
	int tmp;
	/* First of all, write to parent your array_iterator */
	/* That's in order to allow to parent md estimate who is talking to him*/
	MD_PRINT(DPATH,"<md_listen_parent> writing our array_it\n");
	if ((tmp=write(sd,(void *) &md_entity.array_it, sizeof(int))) < sizeof(int)) {
		MD_SYS_ERROR("<md_listen_parent> : write");
	};
	MD_PRINT(DPATH,"<md_listen_parent> %d bytes was written\n",tmp);
	while (i < md_entity.ch_num ) {
		md_read_string(&md_entity.childrenList[i].hostname,sd);
		MD_PRINT(DPATH,"<md_listen_parent> Reading entry No. %d\n",i);
		MD_PRINT(DPATH,"Hostname received: %s\n",md_entity.childrenList[i].hostname);
		
		md_read_string(&md_entity.childrenList[i].device,sd);
		MD_PRINT(DPATH,"Device received: %s\n",md_entity.childrenList[i].device);
		
		md_read_int(&md_entity.childrenList[i].port,sd);
		MD_PRINT(DPATH,"Port received: %d\n",md_entity.childrenList[i].port);
		
		md_read_int(&md_entity.childrenList[i].rank,sd);
		MD_PRINT(DPATH,"Rank received: %d\n",md_entity.childrenList[i].rank);
		MD_PRINT(DPATH,"-----------<md_listen_parent> Entry %d was succesfully recieved\n",i);
		++i;
	}
}

/************ Utility functions ***************************/
int md_read_gen_type(void * buff,size_t len,int sd, int attempt_no) {
	fd_set mask;
	int res, n = 0;
	struct timeval timeout;
	static const int time_sec = 20;
	
	while (n < len) {
		n += (res = read(sd,buff+n,len));
		if (res > 0) {
			MD_PRINT(DPATH,"<md_read_gen_type> : got %d bytes from socket %d\n",res,sd);
		}
		else if (res < 0) {
			if ((errno == EINTR) || (errno == EAGAIN) ) { 
				n-=res;
			}
			else
			{
				return res;
			}
		}
		else if (--attempt_no == 0) {
			return 0;
		}
	}
	return n;
	
}

void md_read_int(int * buff, int sd) {
	md_read_gen_type((void*) buff, sizeof(int),sd,attempt_no);
	MD_PRINT(DDEBUG,"<md_read_int> Int received is %d\n",*buff);
}

void md_read_string(char ** buff,int sd) {
	int str_len;
	md_read_int((void*) &str_len,sd);
	MD_PRINT(DDEBUG,"The str_len received is %d\n",str_len);
	if (str_len < 0)
		MD_SYS_ERROR("<md_read_string> Received string length is negative ! Exiting...\n");
	if ( (*buff = malloc(str_len+1)) == NULL) {
		MD_SYS_ERROR("<md_read_string>,malloc()");
	}
	(*buff)[str_len] = '\0';
	md_read_gen_type((void *) *buff,str_len,sd,attempt_no);
	MD_PRINT(DDEBUG,"<md_read_string> : the buffer received is %s\n",*buff);
}



/* Counts the exact number of DIFFERENT NODES, except of the first.
That is, for the array  {host1, host1, host2, host3, host3, host4 } the output should be 3
*/
int md_get_node_counter() {
	ChildrenListIterator it;
	int md_counter = 0;
	MD_PRINT(DDEBUG, "<md_get_node_counter> getListNextNode(0,1)=%d\n",getListNextNode(0,1));
	for (it = getListNextNode(0,1) ; it < md_entity.ch_num; it=getListNextNode(it,1)) {
			++md_counter;
	}
	return md_counter;
}


/* Launch the tree of minidaemons over the cluster */
#define NPROC_LEN 8
#define WIDTH_LEN 8
void md_start_node_tree() {
	char  * minidaemon_command;
	minidaemon_command = mkstr("%s%s%s", XSTR(MPI_PREFIX),XSTR(MPI_BIN), XSTR(MINIDAEMON_CLIENT_NAME) );
	/* STR_CONCAT(XSTR(MPI_PREFIX), XSTR(MPI_BIN), XSTR(MINIDAEMON_CLIENT_NAME)) ; XSTR(MINIDAEMON_CLIENT_NAME) ; */
	int nproc, width;
	char nproc_str[NPROC_LEN];
	char root_nproc_str[NPROC_LEN];
	char width_str[WIDTH_LEN];
	char mpiport_str[NPROC_LEN];
	char ppid_str[NPROC_LEN];
	char array_it_str[NPROC_LEN];
	char num_of_params_str[NPROC_LEN];
    int md_node_counter; 
    ChildrenListIterator i, j, own_jobs_num;
    int md_step;
    int md_total;
	
	/*  It's very important caclulation
		According to the algorithm, the minidaemon should run its own jobs , then 
		divide the rest of nodes array between (maximum) width minidaemons.
		For example, if nodes array equals to { host1, host1, host2, host2, host3, host4 ,host5, host5}host5, host5}, and width =2,
		than minidaemon on host1 (MD1) will run MD2 with array= {host2,host2,host3 } and MD4 with array = {host4 ,host5, host5}
		After, MD2 will run an additional MD3 with array={host3}  (lead mindaemon ) and MD4 will run an additional MD5 with 
		array = {host5, host5}
	*/

    md_node_counter = md_get_node_counter();
	if (md_node_counter < 1) {
		md_entity.isLeaf = 1;
		MD_PRINT(DDEBUG,"We have reached the bottom level : this minidaemon is a leaf\n");
		return ;
	}

	i = getListNextNode(0,1);
	md_step = (md_node_counter / md_entity.tree_width) + (md_node_counter % md_entity.tree_width ? 1 : 0);
	md_total = md_node_counter;
	MD_PRINT(DDEBUG,"Starting node tree, total node counter is %d, md_step = %d \n",md_node_counter,md_step);
	while (md_node_counter > 0) {
		md_node_counter -= md_step;
		nproc = getListNextNode(i,md_step)-i;
		md_entity.childrenList[i].child_list_len = nproc;
		MD_PRINT(DDEBUG,"<md_start_node_tree> nproc=%d , i=%d\n",nproc,i);
		assert (nproc > 0);
		
		sprintf(width_str, "%d " ,md_entity.tree_width);
		sprintf(nproc_str, "%d " , nproc);
		sprintf(root_nproc_str, "%d ", md_entity.root_ch_num);
		sprintf(mpiport_str, "%d ", md_entity.mpirun_port);
		sprintf(ppid_str, "%d ", md_entity.ppid);
		sprintf(array_it_str,"%d ", i);
        sprintf(num_of_params_str,"%d ", md_entity.mpi_num_of_params);
		
		md_entity.childrenList[i].pid=fork();
		if (md_entity.childrenList[i].pid == 0) {
			int j;
			MD_PRINT(DDEBUG,"Starting Child Minidaemon No. %d of %d at host number %s ,Exec command : %s Parent %d\n",
				md_total-md_node_counter, md_total,md_entity.childrenList[i].hostname, minidaemon_command, md_entity.isParent);
	
			if (!md_entity.isParent)
				close(md_entity.parent_fd);
		
             MD_PRINT(DDEBUG,"Running RSH/SSH: %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s\n",
                    md_entity.remote_sh_command, 
                    md_entity.remote_sh_command, 
                    md_entity.remote_sh_args, 
                    md_entity.childrenList[i].hostname ,
                    minidaemon_command , 
                    md_entity.childrenList[0].hostname, 
                    nproc_str, 
                    width_str, 
                    md_entity.remote_sh_command, 
                    md_entity.remote_sh_args, 
                    md_entity.root_hostname, 
                    root_nproc_str, 
                    mpiport_str, 
                    ppid_str, 
                    array_it_str, 
                    md_entity.wd, 
                    num_of_params_str,
                    md_entity.mpi_params, 
                    md_entity.command );

            if (strcmp(md_entity.remote_sh_args,"NOPARAM")) {
                execl(md_entity.remote_sh_command, 
                        md_entity.remote_sh_command,
                        md_entity.remote_sh_args, 
                        md_entity.childrenList[i].hostname,
                        minidaemon_command, 
                        md_entity.childrenList[0].hostname,
                        nproc_str, 
                        width_str, 
                        md_entity.remote_sh_command, 
                        md_entity.remote_sh_args,
                        md_entity.root_hostname, 
                        root_nproc_str,
                        mpiport_str, 
                        ppid_str, 
                        array_it_str,
                        md_entity.wd,
                        num_of_params_str, 
                        md_entity.mpi_params, 
                        md_entity.command, NULL);
            } else {
                execl(md_entity.remote_sh_command, 
                        md_entity.remote_sh_command,
                        md_entity.childrenList[i].hostname,
                        minidaemon_command, 
                        md_entity.childrenList[0].hostname,
                        nproc_str, 
                        width_str, 
                        md_entity.remote_sh_command, 
                        md_entity.remote_sh_args,
                        md_entity.root_hostname, 
                        root_nproc_str,
                        mpiport_str, 
                        ppid_str, 
                        array_it_str,
                        md_entity.wd, 
                        num_of_params_str, 
                        md_entity.mpi_params, 
                        md_entity.command, NULL);
            }
			/*If we've reached this line, ssh failed*/
			MD_SYS_ERROR ("RSH/SSH command failed!");
		}
		i += nproc;
		for (j = 0 ; j < i ; ++j) { 
			md_entity.childrenList[j].proc_state = MD_NORMAL; /* TODO insert negative number to status message */
		}
		md_entity.total_running_mds +=1;
		own_jobs_num = getListNextNode(0,1);
		md_entity.children_md_arr[own_jobs_num + md_entity.total_running_mds].stat = MD_WORK;
		md_entity.children_md_arr[own_jobs_num + md_entity.total_running_mds].resp_stat = !resp_status_def;
		MD_PRINT(DDEBUG,"own_jobs_num=%d, md_entity.total_running_mds=%d, !resp_status_def = %d\n",
			own_jobs_num,md_entity.total_running_mds,!resp_status_def);
	}

	return ; /* parent process */
}

void md_run_own_jobs() {
	
	ChildrenListIterator i=0;
	
	const int add_param_len = 45; /* the lengths of VIADEV_DEVICE etc. */
	const int max_rank_len = 10; /* the maximum length of rank number in symbols , i.e. 1000000 mpi jobs */
	const int max_port_len = 8;
	char * curr_command_params = NULL;
	char * curr_command = NULL;
	char rank_str[max_rank_len];
	char port_str[max_port_len];
	int curr_param_len = 0;

	do {
		int pd[2];
			
		/* create pipe in order to communicate with children process */
		if (pipe(pd) == -1) {
			MD_SYS_ERROR("pipe");
		}
		md_entity.children_md_arr[i].fd = pd[0];
		md_build_command_string(i/*,&curr_command*/);
		MD_PRINT(DDEBUG,"<md_run_own_jobs> : Starting children processes\n");
		if ((md_entity.childrenList[i].pid = fork())== 0) {  /* Affinity should be set in vianit.c */
            char **params;
			close(pd[0]); /* closing read side of pipe in the child process */
			
			/* close all other filedescriptors, i.e. all except */
			int j;
			for (j = 0 ; j < i ; ++j) {
				close(md_entity.children_md_arr[i].fd);
			}
			
			MD_PRINT(DINFO,"Starting child process: %s\n", md_entity.command);
            setenv_mpi_params();
            MD_PRINT(DDEBUG,"cd: %s\n", md_entity.wd);
            if (chdir(md_entity.wd) < 0) {
                MD_SYS_ERROR("Failed change directory\n");
                exit(MD_EXIT_SYS_ERROR);
            }
            /* Run the application */
            params = str_to_argv(md_entity.command);
            if (NULL == params) {
                MD_SYS_ERROR("Failed to allocate memory for argv\n");
                exit(MD_EXIT_SYS_ERROR);
            }
			execv(params[0], params);
			MD_SYS_ERROR("execl failed");
		} else {
			/* close write side */
			free(curr_command);
			close(pd[1]);
			md_entity.children_md_arr[i].stat = MD_WORK; /* TODO - REPAIR THIS LINE */
			md_entity.childrenList[i].proc_state = MD_NORMAL; 
			md_entity.children_md_arr[i].resp_stat = !resp_status_def;
			++i;
			++md_entity.total_running_jobs; /* TODO = i */
			md_entity.max_fd_num = ( md_entity.max_fd_num > pd[0] ? md_entity.max_fd_num : pd[0]) ;
			
		}
	} while ( i < md_entity.ch_num && (children_list_comp(&md_entity.childrenList[0],&md_entity.childrenList[i]) == 0)) ;
	
	/* Update minidaemon parent entry */
	md_entity.children_md_arr[md_entity.total_running_jobs].fd = md_entity.parent_fd;
	md_entity.children_md_arr[md_entity.total_running_jobs].stat = MD_WORK;
	md_entity.children_md_arr[md_entity.total_running_jobs].resp_stat = !resp_status_def;
	
	/* Free the resources */
	free(curr_command_params);
	curr_command_params = NULL;
	/* TODO while (i < MIN (md_entity.ch_num , getNextListNode(0,1)) */
}

static char** str_to_argv(const char *str) {
    char **argv=NULL;
    char *delims = " \t";
    int count = 0;
    char* word;
    char* l_str=strdup(str);

    if (NULL == l_str) {
        MD_SYS_ERROR("Failed to allocate memory for l_str in md_str_to_argv\n");
        return NULL;
    }

    word = strtok(l_str, delims);
    while (NULL != word) {
        count++;
        word  = strtok(NULL, delims);
    }

    argv = (char**)malloc(sizeof(char*) * (count + 1)); /* +1 for NULL */
    if (NULL == argv) {
        MD_SYS_ERROR("Failed to allocate memory for argv in md_str_to_argv\n");
        return NULL;
    }

    count = 0;
    free(l_str);
    l_str=strdup(str);
    word = strtok(l_str, delims);
    MD_PRINT(DDEBUG, "Adding param to argv : %s\n", word);
    while (NULL != word) {
        argv[count] = strdup(word);
        if (NULL == argv[count]) {
            MD_SYS_ERROR("Failed to allocate memory for argv element in md_str_to_argv\n");
            return NULL;
        }
        count ++;
        word  = strtok(NULL, delims);
        MD_PRINT(DDEBUG, "Adding param to argv : %s\n", word);
    }
    argv[++count] = NULL;
    return argv;
}

char* md_argv_to_string(const int start, const int end, char **argv ) {
    int i;
    char *res = NULL;
    for (i = start; i < end; i++) {
        res = add_word_to_string(res, argv[i]);
    }
    return res;
}

#define STR_DEF_LEN 128
static char* add_word_to_string(char *str, const char *elem )
{
    int str_left;
    int elem_len;

    if (NULL == str) {
        if (NULL == (str = (char*)malloc(sizeof(char) * STR_DEF_LEN))) {
            MD_SYS_ERROR("Malloc of str failed in md_add_to_string\n");
        }
        str_left = STR_DEF_LEN - 1;
    } else {
        str_left = STR_DEF_LEN - (strlen(str) + 1) - 1;
    }

    elem_len = strlen(elem);
    if (elem_len > str_left) {
        int len =
            (STR_DEF_LEN > elem_len + 1 ? STR_DEF_LEN : elem_len + 1) + strlen(str);
        if ((str = realloc(str, len)) == NULL) {
            MD_SYS_ERROR("Realloc failed in md_env_to_string\n");
        }
        str_left = STR_DEF_LEN - 1;
    }
    str = strcat(str, " ");
    str = strcat(str, elem);
    return str;
}

static void setenv_mpi_params()
{
    char *env_last;
    char env_delims[] = "=";
    char *env_list = NULL;
    char *elem_last;
    char list_delims[] = " \t";
    char *elem_list = strdup(md_entity.mpi_params);

    env_list = strtok_r(elem_list, list_delims, &elem_last);
    while (NULL != env_list) {
        char *name, *value;
        name  = strtok_r(env_list, env_delims, &env_last);
        if (NULL == name ) {
            MD_SYS_ERROR("Failed to read name parametr\n");
        }

        value = strtok_r(NULL, env_delims, &env_last);
        if (NULL == value) {
            MD_SYS_ERROR("Failed to read value parametr\n");
        }
        /* Setting envarement */
        MD_PRINT(DDEBUG,"Setenv: %s=%s\n", name, value);
        setenv(name, value, 1);
        env_list = strtok_r(NULL, list_delims, &elem_last);
    }
}

void md_build_command_string(int i /*, char ** exec_command*/) {
    char *xterm_command;
    char xterm_title[100];
    char *ld_library_path;
    char *device_port_env=NULL;
	static char wd[MAX_WD_LEN]="\0";
	
	const int max_rank_len = 10; /* the maximum length of rank number in symbols , i.e. 1000000 mpi jobs */
	const int max_port_len = 8;
	const int max_pid_len = 10;
	const int max_ch_num_len = 10;
	/* const int max_mpirun_port_len = 8; */
	char rank_str[max_rank_len] /*= "\0"*/;
	char port_str[max_port_len] /*= "\0"*/;
	char pid_str[max_pid_len] /*= "\0"*/;
	char ch_num_str[max_ch_num_len] ;
	char mpirun_port_str[max_port_len] ;
	
#define MAX_DISPLAY_LEN 200
	static char display[MAX_DISPLAY_LEN] = "\0";
#define BASE_ENV_LEN 17
    int str_len, len;
	
	int xterm_on = 0;
	
	if (wd[0] == '\0')
		getcwd(wd, MAX_WD_LEN);
	if (display[0] == '\0') {
		get_display_str(display);
	}
	putenv(display);
    
	if (md_entity.childrenList[i].device != NULL && strlen(md_entity.childrenList[i].device ) != 0) {
		setenv("VIADEV_DEVICE",md_entity.childrenList[i].device,1);
	}
 
	if (md_entity.childrenList[i].port != -1) {
		sprintf(port_str,"%d",md_entity.childrenList[i].port);
		setenv("VIADEV_PORT",port_str,1);
	}
	
    ld_library_path = getenv("LD_LIBRARY_PATH");
	
	MD_PRINT(DDEBUG,"LD_LIBRARY_PATH=%s\n",ld_library_path);
	if (ld_library_path != NULL) {
		setenv("LD_LIBRARY_PATH",ld_library_path,1);
	} else {
		setenv("LD_LIBRARY_PATH",LD_LIBRARY_PATH_MPI,1);
	}
    
    /* 
     * this is the remote command we execute whether we were are using 
     * an xterm or using rsh directly 
     */
	sprintf(rank_str,"%d",md_entity.childrenList[i].rank);
	setenv("MPIRUN_RANK",rank_str,1);
	
	setenv("MPIRUN_MPD","0",1);
	setenv("MPIRUN_HOST",md_entity.root_hostname,1);
	MD_PRINT(DDEBUG,"<md_build_command_string> md_entity.root_hostname=%s\n",md_entity.root_hostname);
	
	sprintf(ch_num_str,"%d",md_entity.root_ch_num);
	setenv("MPIRUN_NPROCS",ch_num_str,1);
	
	
	sprintf(pid_str,"%d",md_entity.ppid);
	setenv("MPIRUN_ID",pid_str,1);
	
	sprintf(mpirun_port_str,"%d",md_entity.mpirun_port);
	setenv("MPIRUN_PORT",mpirun_port_str,1);

	putenv("NOT_USE_TOTALVIEW=1");
    
}

/* TODO rebuild minidaemonListen() to be a "Class" with general methods, printf("Hostname received: %s\n",passed by parameters */

void md_general_listen() {
	/* Registrate Event Handlers, if any */
	/* listen to all fd'md_general_listen()s, including self-pipes, if any */
	int status,
		res,
		stat,
		j;
	time_t time_wasted;
	char msg_buf[MAX_MESSAGE_SIZE];
	fd_set wr_mask;
	struct timeval timeout;
	
	md_entity.send2father_timeout 	= send2father_work_timeout;
	md_entity.recv_parent_timeout 	= recv_parent_work_timeout;
	md_entity.recv_children_timeout = (md_entity.isLeaf ? NO_TIMEOUT :recv_children_work_timeout );
	md_entity.recv_jobs_timeout 	= recv_jobs_work_timeout;
	
	md_entity.curr_running_jobs 	= md_entity.total_running_jobs;
	md_entity.curr_running_mds 		= md_entity.total_running_mds;
	
	md_entity.pid 	= getpid();
	timeout.tv_usec = 0;
	
	MD_PRINT(DDEBUG,"Minidaemon is starting to listen...\n");
	MD_PRINT(DDEBUG,"The sockets table for process No. %d are:",md_entity.pid);
	for (j=0; j < 1 + md_entity.total_running_jobs + md_entity.total_running_mds; ++j)
		MD_PRINT(DDEBUG,"%d, ",md_entity.children_md_arr[j].fd);
	/* fprintf(stderr,"\n"); */
	
	/*signal(SIGPIPE, sigpipe_handler);*/
	signal(SIGTERM, sigpipe_handler);
	signal(SIGHUP, 	sigpipe_handler);
	while (1) {
		/* Wait for father and children to respond, no more than fixed timeout */
		time_wasted = time(NULL);
		md_rebuild_mask(&wr_mask); /* should be also general function */
		static int count = 0;
		MD_PRINT(DDEBUG,"%d: send2father_timeout=%d ,recv_children_to=%d, pid = %d\n",
		count++,md_entity.send2father_timeout,md_entity.recv_children_timeout,md_entity.pid);
		if (md_entity.phase == MD_WORK) {
			timeout.tv_sec = MIN(md_entity.send2father_timeout,md_entity.recv_children_timeout);
		}
		else {
			if (md_entity.isParent)
				timeout.tv_sec = ( md_entity.recv_jobs_timeout == NO_TIMEOUT ? md_entity.recv_children_timeout :
				 MAX(md_entity.recv_jobs_timeout,md_entity.recv_children_timeout));
			else
				timeout.tv_sec = MIN(md_entity.recv_jobs_timeout,md_entity.recv_children_timeout);
		}
		MD_PRINT(DDEBUG,"Setting current select timeout to %d\n",timeout.tv_sec);
		switch (res =  select(md_entity.max_fd_num+1,&wr_mask,NULL,NULL,&timeout)) {
			case -1:
				/* Signal or error was received */
				perror("<md_general_listen()> Unhandled signal\n");
				if (md_entity.isParent)
					md_print_status_message();
				exit(MD_EXIT_MINIDAEMON_SIG);
			case 0:
				/* Is parent timeout ?  */
				timeout_handler((int)(time(NULL) - time_wasted));
				break;
			default:
				/* wait for confirmation status from children */
				assert(res > 0);
				int i;
				MD_PRINT(DDEBUG,"Got message from own jobs or other minidaemons : handling...\n");
				/* Find all file descriptors that had been updated */
				for (i = 0 ; i < md_entity.total_running_mds + md_entity.total_running_jobs + 1; ++i) {
					if (FD_ISSET(md_entity.children_md_arr[i].fd, &wr_mask)) {
						/* Child process was ended */
						/* TODO : spin this block out as a separate function */
						
						if (i < md_entity.total_running_jobs) {
							/* TODO set appropriate timeout */
							/* TODO handle different cases */
							MD_PRINT(DDEBUG,"One of children processes[%d] was ended\n",i);
							md_entity.children_md_arr[i].stat = MD_do_not_set;
							md_children_proc_handler(i,(int)(time(NULL) - time_wasted));
						}
						/* Parent or children minidaemons had sent us a message */
						else {
							MD_PRINT(DDEBUG,"Parent or children minidaemons had sent us a message\n");
							md_handler(md_entity.children_md_arr[i].fd,i); /*for parent and children md messages */
							md_update_socket_status(i,(int)(time(NULL) - time_wasted)); 
						}	
						/* TODO : res optimization */
						/*if(--res == 0 )
							break;*/
					}
				} /*  for */
				break;
		} /* switch */
	} /* while */
}
	
void timeout_handler (int delta_time) {

	/* in this case, timeout_ch is a current "receive-from-children-or-parent" timeout
	 and timeout_other is timeout of sending to parent */
	switch(md_entity.phase) {
	case MD_WORK :
		if (md_entity.send2father_timeout < md_entity.recv_children_timeout) {
			if (!md_entity.isParent) {
				MD_PRINT(DINFO,"<timeout_handler> Sending status to parent, pid=%d\n",md_entity.pid);
				md_send_status_message(md_entity.total_running_jobs,-1);
			}
			md_entity.send2father_timeout 	= send2father_work_timeout;
			md_entity.recv_children_timeout -= delta_time;
			assert(md_entity.recv_children_timeout > 0);
		}
		/* We got timeout from children and/or parent */
		else {
			md_start_termination_process(); 
			md_entity.recv_children_timeout = (md_entity.isLeaf ? NO_TIMEOUT :recv_children_term_timeout);
			md_entity.recv_jobs_timeout 	= recv_jobs_term_timeout;
		}
		break;
		
	/* in this case, timeout_ch is a current "receive-from-chilren-or-parent" timeout
	 and timeout_other is timeout for own-jobs-termination	*/
	case MD_TERM :
		/* here we should wait only for children process and children minidaemons */
		/* TODO propagation delay, i.e. to_value = f(base_to_value,tree_depth) */
		sleep(1);
		if (md_entity.total_running_jobs !=0) {
			md_forced_cleanup_handler();
		}
		/* the last status Message we've send */
		/*TODO Send Timeout with forced FINISH */
		if (!md_entity.isParent) {
			md_send_status_message(md_entity.total_running_jobs,-1);
		}
		MD_PRINT(DINFO,"Minidaemon exits right now, pid=%d\n",md_entity.pid);
		if (md_entity.isParent) {
			md_print_status_message();
		}
		sleep(1);
		md_socket_close();
		exit(md_entity.md_exit_value);
		
	default:
		break;
	}
}

void md_children_proc_handler(int ind, int time_delta) {

 	/* static int counter = -1 ; */
	int loc_state;
	/*md_entity.children_md_arr[ind].stat = */ 
	waitpid (md_entity.childrenList[ind].pid,&loc_state,WNOHANG);
	MD_PRINT(DDEBUG,"<chidlren_proc_handler> Job %d was finished with status %d\n",md_entity.childrenList[ind].pid, WEXITSTATUS (loc_state));
	md_entity.childrenList[ind].proc_state = (WIFEXITED(loc_state) ? WEXITSTATUS (loc_state) : -1);
	MD_PRINT(DINFO,"The exit status of job %d is %d\n",ind,md_entity.childrenList[ind].proc_state);
	md_entity.md_exit_value = (md_entity.childrenList[ind].proc_state == MD_EXIT_NORMAL 
		? md_entity.md_exit_value : MD_EXIT_MINIDAEMON_FAIL);

	--md_entity.curr_running_jobs;
	
	if ( md_entity.curr_running_jobs == 0) { /* the last job */
		md_entity.recv_jobs_timeout = NO_TIMEOUT;
		md_entity.recv_children_timeout -= time_delta;
		MD_PRINT(DDEBUG,"<chidlren_proc_handler> The last own jobs was finished with pid %d\n",md_entity.childrenList[ind].pid);
		if (md_entity.curr_running_mds == 0) {
			MD_PRINT(DDEBUG,"<md_children_proc_handler> All own jobs and children minidaemons finished : exiting\n");
			md_entity.phase = MD_FINISH;
			if (md_entity.isParent) {
				md_print_status_message(); 
			} else {
				if (!md_entity.isParent) {
					md_send_status_message(md_entity.total_running_jobs, ind);
				}
			}
			MD_PRINT(DINFO,"<md_children_proc_handler> All own jobs and children minidaemons finished : exiting\n");
			exit(md_entity.md_exit_value);
		}
	}
	if (md_entity.curr_running_jobs == (md_entity.total_running_jobs-1) ) {
		md_entity.phase = MD_TERM;
		md_entity.recv_jobs_timeout 	= recv_jobs_term_timeout;
		md_entity.recv_children_timeout = recv_children_term_timeout;
		
	} /* "else" statement is bug when curr_running jobs == 1 ! */
	else {
		md_entity.recv_jobs_timeout		-= time_delta;
		md_entity.recv_children_timeout -= time_delta;
	}
	if (!md_entity.isParent) {
		md_send_status_message(md_entity.total_running_jobs, ind);
	}
}

void md_print_status_message() {
	int i;
	if (md_entity.md_exit_value == MD_EXIT_NORMAL) {
		MD_PRINT(DDEBUG,"All user jobs finished normally. Minidaemon will shutdown now\n");
	}
	else {
		MD_PRINT(DDEBUG,"MPI run finished,printing the exit status of all mpi jobs: ");
		for (i = 0; i < md_entity.ch_num ; ++i) {
			MD_PRINT(DDEBUG,"%d ", md_entity.childrenList[i].proc_state);
		}
		/* fprintf(stderr,"\n"); fflush(stderr); */
	}
}

/* TODO array of pointer to appropriate function , i.e. func_array[msg_buff[0]].handler(); */
void md_handler(int sd, int i) {

	static int msg_buf[MAX_MESSAGE_SIZE]; /* TODO spin it out */
	static const int small_msg_size = sizeof(int) * 2;
	static const int big_msg_size = sizeof(int) * 4;
	int n = 0; 
	static const short int array_it_index = 1;
	
	n = md_read_gen_type(msg_buf,small_msg_size,sd, attempt_no);
	/* Connection to minidaemon with socket sd was closed */
	if (n <= 0) {
		/* update status table */
		--md_entity.curr_running_mds;
		md_entity.children_md_arr[i].stat = MD_FINISH;
		md_start_termination_process();
		return;
	}
	MD_PRINT(DDEBUG,"Handling incoming message of size %d\n",n);
	/* TODO Once we got msg_buf[0], we can read the rest of the data (md_read_gen_type) according to its type */
	switch ( msg_buf[0]) {
		
		case STATUS_REQUEST:
			MD_PRINT(DDEBUG,"<md_handler> Status request was received\n");
			break;
		case STATUS:
			MD_PRINT(DDEBUG,"<md_handler> Status message was received\n");
			if (msg_buf[array_it_index] != -1) {
				n += md_read_gen_type((void *) &msg_buf[2],big_msg_size - small_msg_size,sd,attempt_no);
			}
			md_status_msg_handler(msg_buf,i, n);
			break;
		case KILL:
			MD_PRINT(DDEBUG,"<md_handler> KILL message was received\n");
			md_start_termination_process();
			break;
		default:
			MD_USR_ERROR("<minidaemon_message_handler> : Bad opcode received\n");
	}
	return;
	
}

/* 
	Message structure :
	Byte1 : index of child minidaemon in the list of parent minidaemon
	Byte2 : OFFSET (-1 in the case of void message)
	Byte3 : Status value (void if previous byte is equal to -1)
*/
void md_status_msg_handler(const int * msg_buf,int i, int size) {
	static const short int array_it_index = 1;
	static const short int offset_index = 2;
	static const short int proc_stat_val_index = 3;
	int iter;
	MD_PRINT(DDEBUG,"Status message received\n");
	assert(size >= sizeof(int)*2);
	if (size == sizeof(int)*2) {
		if (msg_buf[array_it_index] != -1) {
			MD_SYS_ERROR("<md_status_msg_handler> : message too short\n");
		}
		if (i != md_entity.total_running_jobs) {  /* If parent had answered us, there's no need to send him msg right now */
			md_send_status_message(i,-1);
		}
		return ;
	}
	assert (size == sizeof(int)*4) ; 
	iter = msg_buf[array_it_index] + msg_buf[offset_index];
	MD_PRINT(DINFO,"<md_status_msg_handler> Setting status %d to index %d\n", msg_buf[proc_stat_val_index],iter);
	md_entity.childrenList[iter].proc_state = msg_buf[proc_stat_val_index];
	md_entity.md_exit_value = (md_entity.childrenList[iter].proc_state == MD_EXIT_NORMAL 
		? md_entity.md_exit_value : MD_EXIT_MINIDAEMON_FAIL);
	if (i != md_entity.total_running_jobs) {  /* If parent had answered us, there's no need to send him msg right now */
		md_send_status_message(i,-1);
		if (!md_entity.isParent) {
			md_send_status_message(md_entity.total_running_jobs,iter); /*update the parent*/
		}
	} 
}

/* This message will be used for :
1. Implementation of the I-AM-ALIVE protocol. In this case, information about child jobs of the sender may remain unchanegd
2. Notification on status changing of one or more processes. In this case, a status of an appropriate job will be changed
*/
void md_send_status_message(int md_index, ChildrenListIterator iter) {
	const int status_buff_len = 4;
	static int buff[] = {STATUS,-1,-1,-1};
	int write_size = status_buff_len * sizeof(int);
	int sd = md_entity.children_md_arr[md_index].fd;
	
	if (iter < 0) { /* Send only I-AM-ALIVE message */
		buff[1] = -1;
		write_size = sizeof(int) * 2;
	} else {
		buff[1] =  md_entity.array_it ;
		buff[2] =  iter;
		buff[3] =  md_entity.childrenList[iter].proc_state;
		write_size = sizeof(int) * status_buff_len;
		MD_PRINT(DINFO,"Sending status message to socket %d ,md_entity.array_it =%d,iter =%d, proc_state=%d\n",
			sd,md_entity.array_it,iter, md_entity.childrenList[iter].proc_state);

	}
	if (write(sd,buff,write_size) < write_size ) {
		if (md_entity.phase != MD_TERM)
			MD_SYS_ERROR("<md_send_status_message>");
	}
	
}

/* TODO : define which set - parent, md or proc children should be set */
void md_rebuild_mask(fd_set * mask) {

	int i;
	FD_ZERO(mask);
	/* TODO not curr_running, but total_running; */
	MD_PRINT(DDEBUG,"<md_rebuild_mask>\n");
	for (i = 0 ; i < md_entity.total_running_jobs; ++i) {
		if (md_entity.children_md_arr[i].stat != MD_do_not_set) {
			MD_PRINT(DDEBUG,"<md_rebuild_mask> mask for job %d was set\n",i);
			FD_SET (md_entity.children_md_arr[i].fd, mask) ;
		}
	}
	
	for (i = md_entity.total_running_jobs ; i <  md_entity.total_running_mds + md_entity.total_running_jobs +1; ++i) {
		if (i == md_entity.total_running_jobs && md_entity.isParent)
			continue; /* do not set "parent" socket for root minidaemon */
		MD_PRINT(DDEBUG,"<md_rebuild_mask> i = %d ,	md_entity.children_md_arr[i].stat= %d, \
md_entity.children_md_arr[i].resp_stat= %d , resp_status_def=%d\n",
				i,md_entity.children_md_arr[i].stat,md_entity.children_md_arr[i].resp_stat,resp_status_def);
				
		if ((md_entity.children_md_arr[i].resp_stat != resp_status_def) && (md_entity.children_md_arr[i].stat == MD_WORK)) {
			MD_PRINT(DDEBUG,"<md_rebuild_mask> mask for md %d was set\n",i);
			FD_SET (md_entity.children_md_arr[i].fd, mask) ; 
		}
	}
}


int md_update_socket_status(int i,int time_delta) {

	int res=0;
	static int num_of_md_responded = 0;
	MD_PRINT(DDEBUG,"<md_update_socket_status> : index = %d, delta = %d, my pid = %d\n",i, time_delta, md_entity.pid);
	
	if (md_entity.children_md_arr[i].resp_stat != resp_status_def) {
		md_entity.children_md_arr[i].resp_stat = resp_status_def;
		++num_of_md_responded;
	
		if (res=(num_of_md_responded == (md_entity.total_running_mds+ (!md_entity.isParent))))  { /* all mds and parent */
			num_of_md_responded = 0;
			resp_status_def = !resp_status_def; /* We change the definition of resp_status_def instead of all the status array*/
			md_entity.recv_children_timeout = recv_children_work_timeout;
		}
		else /*if (num_of_md_responded == 0)*/ {
			md_entity.recv_children_timeout-=time_delta;
		}
	}
	md_entity.send2father_timeout-=time_delta;
	return res;
}

void md_start_termination_process() {
	int i ;
	MD_PRINT(DDEBUG,"<md_start_termination_process> Checking\n");
	if (md_entity.phase != MD_TERM) {
		MD_PRINT(DDEBUG,"Starting termination process, pid=%d\n",md_entity.pid);
		md_entity.phase = MD_TERM;
		for (i = md_entity.total_running_jobs +1 ; i < md_entity.total_running_mds+md_entity.total_running_jobs +1; ++i) {
			/* send status message to other MD (with termination status) */
			md_send_termination_message(md_entity.children_md_arr[i].fd);
		}
	}
}

void md_send_termination_message(int sd) {

	static const int buff[1] = {KILL};
	MD_PRINT(DDEBUG,"Sending termination message to children from process %d: socket %d\n",md_entity.pid, sd);
	if (write (sd,buff,sizeof(int)) < sizeof(int)) {
		MD_SYS_ERROR("write");
	}
}

void md_forced_cleanup_handler() {
	static const int md_killed_job_status = -2;
	/* send kill to its own jobs */
	ChildrenListIterator i;
	int loc_state;
	/* send forced kill (kill -9) to its own jobs */
	MD_PRINT(DINFO,"<md_forced_cleanup_handler> Starting ...\n");
	for (i=0; i < getListNextNode(0,1) ; ++i) {
		int res;
		/* If the result was negative, the error was happen.
		   In General case, we shold exit, but here we must continue job termination,
		   in spite of the error
		  */
		if ((res = waitpid(md_entity.childrenList[i].pid,&loc_state,WNOHANG)) == 0) {
			/* Job is still running */	
			kill(md_entity.childrenList[i].pid,SIGKILL);
			res = waitpid(md_entity.childrenList[i].pid,&loc_state,WNOHANG);
		}
		md_entity.childrenList[i].proc_state = md_killed_job_status;
		if (res > 0) {
			md_entity.childrenList[i].proc_state =  (WIFEXITED(loc_state) ? WEXITSTATUS (loc_state) : -1);
		}
		md_send_status_message(md_entity.total_running_jobs,i);
		MD_PRINT(DINFO,"The exit status of possibly killed job %d is %d\n",i,md_entity.childrenList[i].proc_state );		
	}
}

void md_socket_close() {

	int i;
	for (i = 0; i < md_entity.total_running_jobs; ++i) {
		close(md_entity.children_md_arr[i].fd);
	}
	if (!md_entity.isParent)
		close(md_entity.children_md_arr[md_entity.total_running_jobs].fd);
	
	if (!md_entity.isLeaf)
	for (i = 0; i < 1 + md_entity.total_running_jobs+md_entity.total_running_jobs; ++i) {
		close(md_entity.children_md_arr[i].fd);
	}
		
}

void md_req_status_handler(const char * buff) {
	MD_PRINT(DDEBUG,"Request for status message received, in pid=%d\n",md_entity.pid);
	switch (buff[1]) {
	/*case MD_INIT:
		md_send_init_message();*/
	default:
		md_send_status_message(md_entity.total_running_mds,-1);
	}
}

/* TODO optimize with receive width times at the client side  */
void md_send_init_message(int fd) {
	MD_PRINT(DDEBUG,"<md_send_init_message> : starting\n");
	ChildrenListIterator it = 0, array_it;
	int len;
	
	/* First of all, we have to know who we are talking to: */
	MD_PRINT(DDEBUG,"Reading array iterator number from child socket no. %d\n",fd);
	md_read_int(&array_it,fd);
	
	MD_PRINT(DDEBUG,"Sending %d childrenList entries to child to socket no. %d\n",md_entity.childrenList[array_it].child_list_len,fd);
	MD_PRINT(DDEBUG,"array_it = %d, md_entity.childrenList[array_it].child_list_len = %d\n", 
		array_it,  md_entity.childrenList[array_it].child_list_len);
		
	for (it = array_it; it < (md_entity.childrenList[array_it].child_list_len + array_it) ; ++it) {
		len= (md_entity.childrenList[it].hostname == NULL 
			? 0 :  strlen(md_entity.childrenList[it].hostname)*sizeof(char));
		MD_PRINT(DDEBUG,"The len to send is %d\n",len);
		if (write(fd, &len,sizeof(int)) < sizeof(int))
			perror("write");
		if (write(fd, md_entity.childrenList[it].hostname,len) < len)
			perror("write");
			
		len= (md_entity.childrenList[it].device == NULL 
			? 0 :  strlen(md_entity.childrenList[it].device)*sizeof(char));
		if (write(fd, &len,sizeof(int)) < sizeof(int))
			perror("write");
		
		if (write(fd,md_entity.childrenList[it].hostname,len) < len)
				perror("write");

		if (write(fd,&md_entity.childrenList[it].port,sizeof(int)) < sizeof(int))
			perror("write");
		MD_PRINT(DDEBUG,"<md_send_init_message> : The port is %d\n",md_entity.childrenList[it].port);
		
		if (write(fd,&md_entity.childrenList[it].rank,sizeof(int)) < sizeof(int))
			perror("write");
		MD_PRINT(DDEBUG,"<md_send_init_message> : The rank is %d\n",md_entity.childrenList[it].rank);	
	}
}

void sigpipe_handler(int signo) {

	MD_PRINT(DDEBUG,"Handling SIGPIPE\n");
	md_forced_cleanup_handler();
	if (md_entity.isParent) {
		md_print_status_message();
	}
	exit(MD_EXIT_MINIDAEMON_SIG);
}

static void get_display_str(char * display) {
    char *p;
    char str[200];

    if ( (p = getenv( "DISPLAY" ) ) != NULL ) {
		strcpy(str, p ); /* For X11 programs */  
		sprintf(display,"DISPLAY=%s",str);
    }
}
