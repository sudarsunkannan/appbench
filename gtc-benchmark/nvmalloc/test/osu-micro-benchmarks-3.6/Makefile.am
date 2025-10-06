pkglibexec_PROGRAMS = osu_alltoallv osu_allgatherv osu_scatterv osu_gatherv osu_reduce_scatter osu_barrier osu_reduce osu_allreduce osu_alltoall osu_bcast osu_bibw osu_bw osu_latency osu_mbw_mr osu_multi_lat

osu_alltoallv_SOURCES = osu_alltoallv.c osu_coll.h
osu_allgatherv_SOURCES = osu_allgatherv.c osu_coll.h
osu_scatterv_SOURCES = osu_scatterv.c osu_coll.h
osu_gatherv_SOURCES = osu_gatherv.c osu_coll.h
osu_reduce_scatter_SOURCES = osu_reduce_scatter.c osu_coll.h
osu_allgather_SOURCES = osu_allgather.c osu_coll.h
osu_barrier_SOURCES = osu_barrier.c osu_coll.h
osu_reduce_SOURCES = osu_reduce.c osu_coll.h
osu_allreduce_SOURCES = osu_allreduce.c osu_coll.h
osu_gather_SOURCES = osu_gather.c osu_coll.h
osu_scatter_SOURCES = osu_scatter.c osu_coll.h
osu_alltoall_SOURCES = osu_alltoall.c osu_coll.h
osu_bcast_SOURCES = osu_bcast.c osu_coll.h

if MPI2_LIBRARY
    pkglibexec_PROGRAMS += osu_gather osu_allgather osu_scatter osu_acc_latency osu_get_bw osu_get_latency osu_latency_mt osu_put_bibw osu_put_bw osu_put_latency osu_passive_put_latency osu_passive_put_bw osu_passive_get_latency osu_passive_get_bw osu_passive_acc_latency
endif

if CUDA
dist_pkglibexec_SCRIPTS = get_local_rank
endif

EXTRA_DIST = README CHANGES
