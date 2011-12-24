SHELL		= /bin/sh
prefix		= /usr/local
CC		= $(prefix)/bin/mpicc

.SUFFIXES:
.SUFFIXES: .c .o
.DELETE_ON_ERROR:
.PHONY: mpi1 mpi2 all install clean

MPI1_BENCHMARKS	= osu_alltoall osu_bcast osu_bibw osu_bw osu_latency osu_mbw_mr\
                  osu_multi_lat
MPI2_BENCHMARKS	= osu_acc_latency osu_get_bw osu_get_latency osu_latency_mt\
		  osu_put_bibw osu_put_bw osu_put_latency
ALL_BENCHMARKS	= $(MPI1_BENCHMARKS) $(MPI2_BENCHMARKS)

# By default only MPI-1 Benchmarks are built
mpi1:	$(MPI1_BENCHMARKS)
mpi2:	$(ALL_BENCHMARKS)
all:	$(ALL_BENCHMARKS)

osu_bcast: osu_bcast.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -lm -o osu_bcast osu_bcast.c $(LDLIBS)

install:
	test -d $(prefix)/osu_benchmarks || mkdir $(prefix)/osu_benchmarks
	for benchmark in $(ALL_BENCHMARKS); do\
	    test -x $$benchmark && cp $$benchmark $(prefix)/osu_benchmarks;\
	done

clean:
	rm -f $(ALL_BENCHMARKS) $(ALL_BENCHMARKS:%=%.o)

$(ALL_BENCHMARKS:%=%.o): osu.h
