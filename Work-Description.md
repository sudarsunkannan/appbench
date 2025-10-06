### Graphchi (graph compute engine)
What it runs: PageRank, connected components, ALS, etc., via a sliding-shards engine.

Memory behavior: Reads vertex/edge shards sequentially but performs many random updates to vertex state. Sliding windows transform some randomness into streaming, yet update buffers still hit caches irregularly.

Parallelism: Shared-memory, multi-threaded; parallel per-interval processing.

 Ideal for testing random-vs-sequential trade-offs, cache locality, and the benefit/harm of placing vertex state in faster memory while edges stream from slower memory.

#### Graph500 (MPI BFS)

What it runs: Breadth-first search on synthetic Kronecker/R-MAT graphs; reports TEPS.

Memory behavior: Pointer chasing and frontier sets produce highly irregular access with poor spatial locality.

Parallelism: Distributed MPI (optionally + OpenMP per rank); significant communication and synchronization at level boundaries.

 Very latency-sensitive. Great for studying NUMA placement, page migration, interconnect costs, and slow-tier penalties on random access.

#### Metis (in-memory MapReduce)

What it runs: Map/Reduce analytics jobs with hash aggregation.

Memory behavior: Maps are mostly sequential scans; reduce phases create/merge hash tables with random probes and inserts; noticeable allocation churn.

Parallelism: Threaded mappers/reducers; data partitioning across cores.

 Exposes allocator behavior, cache locality of hash tables, and bandwidth during scans—useful for evaluating tiered allocations (map buffers vs. reduce tables).

#### Redis (in-memory KV)

What it runs: GET/SET workloads; optional persistence.

Memory behavior: Small random ops; pointer-rich structures (SDS, hashtables, ziplists/skiplists); frequent small allocations.

Parallelism: Single-threaded command path (classic), optional I/O threads; strong tail-latency sensitivity.

 Canonical data-serving / caching workload. Excellent for measuring the impact of slow-tier memory on p99 latency and allocator/tcache behavior.

#### LevelDB (LSM KV store)

What it runs: Put/Get; compaction; block cache.

Memory behavior: Hot memtable and block cache in memory; compactions sequentially read/write large files; reads mix random index lookups with sequential block reads.

Parallelism: Foreground ops + background compaction threads.

 Natural hot/cold split: indexes and hot blocks belong in fast memory; SST data can tolerate slower tiers. Great for studying tier placement policies and compaction interference.


#### GTC benchmark (HPC)

What it runs: Representative compute kernel (often particle/stencil heavy).

Memory behavior: Indirect addressing, gather/scatter, and regular sweeps; bandwidth and cache reuse are primary bottlenecks.

Parallelism: MPI across ranks, optionally OpenMP within ranks.

 Highlights benefits of bandwidth and NUMA-aware placement; sensitive to page interleaving and thread binding.
