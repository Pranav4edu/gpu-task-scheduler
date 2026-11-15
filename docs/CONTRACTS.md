# Shared contracts and implementation status

Status snapshot: November 14, 2026. Public declarations are the shared
interfaces. Some modules are implemented and others remain declaration-only;
check the status below rather than assuming every declared function has a
definition. Changes to shared types or contracts require affected owners'
coordination.

## Ownership and integration

| Producer | Consumer | Contract |
| --- | --- | --- |
| Divyaa's parser (pending) | Shaswitha's validator | `ParsedInput` contains task rows in input order and resolved `EdgePair {from,to}` values, with edges directed predecessor to successor. |
| Shaswitha's graph module (implemented) | Engine and scheduler | Immutable successor traversal, original indegrees, topological order, and critical-path ranks. |
| Engine (implemented) | Validator and metrics | One `TaskRecord` per task at its dense index, busy ticks for every unit, and makespan. |
| Engine (implemented) | Aayush's scheduler | Newly ready `ReadyKey` batches in ascending `TaskId` order, with immutable durations, ready times, and ranks. |
| Scheduler | Engine | One unique task per unique idle unit; queue removal occurs once, while the engine owns runtime state transitions. |
| Workload generators (implemented) | Tests and callers | Deterministic `ParsedInput` with dense task order and predecessor-to-successor edges; may be passed to `validate_workload`. |

All interfaces use namespace `gpu` and C++17. Template definitions belong in
headers. Non-template implementations belong in `code/src/`. Keep one
executable entry point in `main.cpp` and add tests to the shared test runner.

## Data structures and graph algorithms

- **BinaryHeap:** implemented. `top()` returns a reference invalidated by heap
  mutation; `pop()` returns a value. Empty access throws
  `std::underflow_error`. It owns vector storage and tracks successful
  operations and heap statistics. Sifting is O(log H); a growth insertion can
  be O(H).
- **RingDeque:** declaration only. The contract is head/count plus array
  storage, wraparound, and doubling growth. Empty access/pop throws
  `std::underflow_error`; pops are O(1), pushes amortized O(1), and resizing is
  O(Q).
- **DirectedGraph:** implemented in `gpu/ds/directed_graph.hpp` and
  `directed_graph.cpp`. It stores `head[N]`, an edge pool of `{to,next}`, and
  original indegrees. `build` validates endpoints and self-edges, sorts the
  by-value edge-pair vector to reject duplicates, then links each edge to the
  previous head. Construction takes O(N + E log E) time because of sorting;
  persistent graph storage is O(N+E). The input edge vector is sorted in place;
  no second E-sized sorting buffer is constructed. Successor traversal costs
  O(outdegree) per vertex, or O(E) across all vertices. Graph construction does
  not establish acyclicity.
- **Kahn topological order:** implemented in `src/dag_algorithms.cpp`. It copies
  original indegrees, initializes zero-indegree vertices in ascending
  `TaskId`, processes a vector queue, and rejects cycles when not all vertices
  are emitted. Time is O(N+E), with O(N) temporary storage.
- **Critical-path ranks:** computed in reverse topological order as task
  duration plus the maximum successor rank. `checked_add` detects tick
  overflow. The helper validates topological-order dimensions and edge
  ordering. Time is O(N+E), with O(N) temporary storage.
- **IdHashTable:** declaration only. Its contract specifies owned hashing,
  bucket heads, entry-pool chains, and rehashing; implementation and collision
  tests remain pending.

## Workload validation

`validate_workload(ParsedInput)` is implemented. It rejects negative releases,
nonpositive durations, invalid endpoints, self-edges, duplicate edges, and
cycles. It preserves task-vector order as dense `TaskId` order, constructs a
`DirectedGraph`, then returns the graph, topological order, and critical-path
ranks in `ValidatedWorkload`. Empty workloads are supported. Its overall time
is O(N + E log E), dominated by graph edge sorting; overall storage is O(N+E),
including the returned graph and temporary vectors.

## Independent trace verification

`verify_result(const ValidatedWorkload&, const RunConfig&, const RunResult&)`
is implemented. It checks:

- result task count and dense TaskId indexing;
- unit IDs, nonnegative timestamps, and exact duration/finish arithmetic;
- ready time equal to `max(task.release, predecessor finish times)` and start
  no earlier than ready;
- nonoverlapping half-open intervals on each unit (adjacent intervals are
  allowed);
- each unit's busy ticks equal to its assigned task durations; and
- makespan equal to the maximum finish time, or zero for an empty workload.

It uses the workload graph's successor traversal for dependency checks and
does not mutate the workload or result. Interval sorting makes verifier time
O(N log N + E + P), where P is the configured compute-unit count; additional
storage is O(N+P).

The verifier checks **feasibility and reported statistics**, not FIFO/SJF/CP
policy ordering or global work conservation. It also does not currently enforce
`TaskRecord.initial_owner` or `stolen` semantics. Those guarantees require
agreement with the engine/scheduler owners before expanding verifier scope.

## Deterministic workload generators

Declared in `gpu/workload_generators.hpp`; all functions return `ParsedInput`,
use dense task IDs, deterministic unique names `T0`, `T1`, …, uniform timing,
and no randomness:

```cpp
ParsedInput make_chain_workload(std::size_t task_count,
                                Tick release = 0, Tick duration = 1);
ParsedInput make_fork_join_workload(std::size_t branch_count,
                                    Tick release = 0, Tick duration = 1);
ParsedInput make_layered_workload(
    const std::vector<std::size_t>& layer_widths,
    Tick release = 0, Tick duration = 1);
```

Chain edges are `i -> i+1`; zero tasks produce an empty input. Fork-join
requires at least one branch and emits a source, parallel branches, and a sink.
Layered workloads connect every task in each layer to every task in the next;
an empty width list is empty, while zero-width layers are rejected. All
generators reject negative release and nonpositive duration, and check count
and edge arithmetic before allocation.

Let N be output tasks, E output edges, B branch count, and L layer count.
Generator record/edge work is O(N+E), plus task-name text construction of
O(N log N) characters in the worst case. Output storage is O(N+E). For
fork-join, N=B+2 and E=2B. For layered graphs,
N=Σ widthᵢ and E=Σ(widthᵢ × widthᵢ₊₁).

## Input format and pending parser

The agreed target input format is:

```csv
task_id,release_time,duration,dependencies
A,0,2,
B,0,3,
C,0,6,
D,0,2,A;B
E,0,4,C
F,0,1,D;E
```

The parser and `load_and_validate` are pending. The parser contract is exactly
four comma-separated fields, semicolon-separated predecessor IDs, no quoted
fields or embedded commas, IDs containing letters/digits/underscores, LF/CRLF,
trimmed whitespace, and two-pass resolution for forward references. Parsing
must reject duplicate/unknown IDs and malformed values with path/row/field
context where applicable. `load_and_validate` must pass the resolved
`ParsedInput` to `validate_workload`, which rejects invalid task times,
duplicate/self edges, cycles, and rank overflow. Dense IDs preserve input-row
order.

## Event and scheduling contract

The engine implements the following event semantics. Production scheduling
policies and the complete CSV pipeline remain pending:

1. Pop every event at the minimum time.
2. Process completions, free units, and decrement successor counters.
3. Process arrivals.
4. Collect newly eligible tasks exactly once and sort by dense ID.
5. Enqueue the batch, then dispatch idle units in ascending unit ID.
6. Record starts and push completion events.

A task is eligible when released and all predecessors have finished. Release
and completion at the same timestamp permit immediate start. Execution is
non-preemptive, durations are positive, and dispatch overhead is zero. Copy
runtime indegrees; do not mutate the graph. `checked_add` guards tick
arithmetic.

- FIFO preserves existing queue entries, then appends the sorted new batch.
- SJF orders by duration, ready time, then TaskId.
- CP orders by descending rank, ready time, then TaskId; compare ranks without
  negation.
- Static FIFO assigns owner `TaskId % P`; only eligible tasks enter local queues.
- Optional stealing gives owners a dispatch opportunity before cyclic victim
  scans; steal from the back and never steal running tasks.

Central FIFO/SJF/CP and static FIFO are required pairs; stealing FIFO is
optional. SJF/CP with local distribution is not supported. Production scheduler
integration and metrics remain pending. Exact durations/ranks are
known offline; this is a GPU-inspired simulation, not physical GPU execution.

## Metrics and test evidence

Ready = max(release, latest predecessor completion); wait = start-ready;
turnaround = finish-release; makespan is measured from zero. Useful utilization
is total duration / (P × makespan). Busy-time CV uses population standard
deviation over all units. Nearest-rank p95 uses sorted position
ceil(0.95 × N). Empty makespan is zero; empty averages and zero-denominator
ratios are N/A. Metrics implementation remains pending.

`code/data/expected/six_tasks.csv` is a hand-calculated oracle, not measured
output. For P=2 and all releases zero, FIFO/SJF makespan is 13, mean wait 2/6,
utilization 18/26. CP makespan is 11, mean wait 3/6, utilization 18/22.
Critical-path ranks are A=5, B=6, C=11, D=3, E=5, F=1.

The graph branch initially reported a Windows execution restriction after
compiling 82 tests. Subsequent GitHub CI passed all 82; this local integration
branch also passes those 82 tests and 18 engine/integration groups. See
`docs/INTEGRATION_CHECK.md` for the combined evidence and remaining scope.

## Engine adapter and assignment checks


`gpu/simulator.hpp` adds `simulate_events(tasks, dependencies, ranks, config,
scheduler, stats)` without changing `DirectedGraph`, `Scheduler` or the planned
two-argument `simulate` API. `DependencyView` borrows the immutable indegree vector
and a synchronous successor visitor; it owns no graph storage. The engine copies
indegrees into per-run state. Graph and callback captures must outlive the call.
See EVENT_ENGINE.md for the implemented graph adapter and test-only schedulers.

Pass a fresh, empty scheduler configured for the same policy/distribution. It
must return all assignments allowed by available work and idle units: central
and stealing modes fill `min(ready tasks, idle units)` slots; static mode fills
each idle unit whose owner queue is nonempty. A scheduler failure invalidates
that scheduler instance; construct a new one before retrying. The engine checks
assignment membership, uniqueness, bounds, ownership and work conservation;
policy ordering and owner-first stealing remain the scheduler's responsibility.

The engine canonicalizes returned assignments by unit ID. `TaskRecord.initial_owner`
equals the first assigned unit in central mode and `TaskId % P` in local modes.
In stealing mode, `stolen` must exactly match execution on a non-owner unit.
CP needs one nonnegative rank per task; FIFO/SJF may omit ranks (keys receive 0).
Graph/DAG and rank correctness are preprocessing responsibilities, not inferred
from the engine's defensive checks. EngineStats holds event-heap counters plus
timestamp, edge-visit, ready-task and dispatch counts; it resets per call and is
published only on success.


On this integration branch, `simulate_with_scheduler(workload, config, scheduler,
stats)` constructs the DependencyView directly from the implemented DirectedGraph
and forwards its validated tasks/ranks to the engine. Real graph results are
checked by verify_result in integration tests and the checkpoint demonstration.
