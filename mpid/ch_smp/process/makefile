srcdir ?= .
mpirun_rsh: LDLIBS =  -lm
CFLAGS = $(MPIRUN_CFLAGS) 
libmembers = \
        pmgr_collective_common.o \
        pmgr_collective_ranges.o \
        pmgr_collective_client.o \
        pmgr_collective_client_common.o \
        pmgr_collective_client_tree.o \
        pmgr_collective_client_mpirun.o \
        pmgr_collective_client_slurm.o \
        minidaemon.o \
        mpirun_util.o

all: mpirun_rsh mpispawn minidaemon_client update_lib
clean:
	-rm -f *.o mpirun_rsh mpispawn minidaemon_client $(libmembers)

minidaemon_client: minidaemon.o mpirun_util.o
mpispawn: mpirun_util.o mpispawn_tree.o pmgr_collective_mpispawn.o
mpirun_rsh: pmgr_collective_common.o pmgr_collective_mpirun.o \
  minidaemon.o mpirun_util.o $(MPIRUN_MAC)

update_lib: $(libmembers)
	$(AR) $(LIBNAME) $(libmembers)

vpath %.c $(srcdir)
