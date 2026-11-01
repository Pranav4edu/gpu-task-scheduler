# Shared contracts

Proposed interface freeze: November 1, 2026. Changes require the affected module
owner's review. Header declarations are the code-level contract; declaration-only
modules must be implemented before calling them.

## Ownership and interfaces

| Producer | Consumer | Contract |
| --- | --- | --- |
| Divyaa's parser | Shaswitha's graph | `ParsedInput`: task records in input-row order and resolved `EdgePair {from,to}` values |
| Shaswitha's graph | Pranav's engine | Immutable successor traversal, original indegrees, topological order and ranks |
| Pranav's heap | Aayush's SJF/CP | `Before(a,b)` means **a is returned first**, the opposite convention to common priority_queue comparators |
| Engine | Aayush's scheduler | Newly ready `ReadyKey` batches in ascending TaskId order; immutable durations, ready times and ranks |
| Scheduler | Engine | One unique task per unique idle unit; queue removal occurs once, but only engine changes runtime state |
| Engine | Validator and metrics | One `TaskRecord` per task at its dense index, busy ticks for every unit and makespan |

All classes/functions are in namespace `gpu`. Use C++17. Template definitions go
in headers. Non-template implementations go in `code/src/`. Keep one executable
entry point in main.cpp; add tests to the shared test runner, not another main.

## Data structures

- **BinaryHeap** is implemented. `top()` returns a reference invalidated by heap
  mutation; `pop()` returns a value. Empty access throws `std::underflow_error`.
  It owns vector storage and counts successful pushes/pops, comparator calls,
  swaps and peak size. Invariant inspections do not affect counters. Counts are
  per heap lifetime; instantiate a new heap for each run. Assumes cheap value
  records and non-throwing comparators. Sifting is O(log H); growth can make one
  insertion O(H); storage follows allocated capacity, not current size after pops.
- **RingDeque** is declaration-only. Head/count plus array storage, capacity at
  least one, end operations, wraparound and doubling growth. Empty access/pop
  throws `std::underflow_error`. Pops are O(1); pushes amortized O(1), with O(Q)
  resizing. Do not shrink initially. Test aliased values during resize.
- **DirectedGraph** is declaration-only. Head indices, `{to,next}` edge pool and
  indegrees. Sort edge pairs to reject duplicates; validate endpoints/self-edges.
  Build costs O(N + E log E); traversal and subsequent DAG passes O(N+E).
  Construction does not itself establish acyclicity. Freeze once built.
- **IdHashTable** is declaration-only. Own hashing, bucket heads, entry-pool chains
  and doubling rehash before load exceeds 0.75. Duplicate insertion returns false
  without replacing the value. Missing lookup returns nullopt. An injected hasher
  allows tests to force collisions. No deletion required. Expected lookup depends
  on key length and hash distribution; collision-heavy worst cases are linear.

The starter SJF/CP comparators are shared integration helpers. Aayush still owns
policy integration, its tests, deque implementation and placement semantics.

## Input format

```csv
task_id,release_time,duration,dependencies
A,0,2,
B,0,3,
C,0,6,
D,0,2,A;B
E,0,4,C
F,0,1,D;E
```

Exactly four comma-separated fields; semicolon-separated predecessor IDs; no
quoted fields or embedded commas. IDs use letters, digits and underscores.
Accept LF/CRLF, trim whitespace and permit forward references via two-pass
parsing. An edge is predecessor -> successor. Reject duplicates, unknown IDs,
self-dependencies, cycles, malformed integers, release < 0, duration <= 0 and
overflow. Errors include path, row and field where applicable. The header-only
fixture is a valid empty workload. Dense IDs preserve input-row order.

## Event and scheduling semantics

1. Pop every event at the minimum time from the custom heap.
2. Process completions and free units; decrement successor counters.
3. Process arrivals and mark tasks released.
4. Collect newly eligible tasks exactly once and sort by dense ID.
5. Enqueue the entire batch, then dispatch idle units in ascending unit ID.
6. Record starts and push one completion event per started task.

A task is eligible when released and all predecessors have finished. Release and
completion at the same timestamp permit immediate start. Durations are positive,
execution non-preemptive, dispatch overhead zero. Copy runtime indegrees; do not
mutate the graph. `checked_add` guards both timestamp and CP-rank arithmetic.

- FIFO preserves existing queue entries, then appends the sorted new batch.
- SJF orders by duration, ready time, TaskId.
- CP orders by descending rank, ready time, TaskId; compare ranks without negation.
- Static FIFO assigns owner = TaskId % P; only eligible tasks enter local queues.
- Optional stealing gives all owners a dispatch opportunity before thieves scan
  cyclic victims. Steal from the back, dispatch immediately and stop after at
  most P-1 probes per thief per epoch. Never steal running tasks.

Required pairs: central FIFO/SJF/CP and static FIFO. Stealing FIFO is optional.
No SJF/CP with local distribution. Exact input durations/ranks are known offline;
this is a GPU-inspired simulation, not a physical GPU execution model.

## Metrics and hand oracle

Ready = max(release, latest predecessor completion); wait = start-ready;
turnaround = finish-release; makespan measured from zero. Useful utilization is
total duration / (P*makespan). Busy-time CV uses population standard deviation
over all units. Nearest-rank p95 uses sorted position ceil(0.95*N). Empty makespan
is zero; empty averages and zero-denominator ratios are N/A.

`code/data/expected/six_tasks.csv` is hand-calculated for P=2, all releases zero.
FIFO/SJF makespan is 13, mean wait 2/6, utilization 18/26. CP makespan is 11,
mean wait 3/6, utilization 18/22. These are expected values for future integration
tests, not measured project results. CP ranks: A=5, B=6, C=11, D=3, E=5, F=1.
