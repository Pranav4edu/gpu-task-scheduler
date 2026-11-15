# GPU Task Scheduler and Load Balancer

CE 6320 Group 4: a deterministic C++17 simulator of dependent tasks on virtual
compute units. No physical GPU or CUDA is required.

## Current checkpoint

The graph and DAG module is implemented: custom adjacency-list graph storage,
Kahn topological sorting, critical-path ranks, workload validation, independent
trace feasibility/statistics validation, and deterministic chain, fork-join,
and layered workload generators.

This is **not yet the complete simulator**. The ring deque and chained hash
table are declaration-only. The CSV parser, `load_and_validate`, event
simulation engine, integrated scheduling policies, metrics, and full experiment
pipeline remain pending. No simulator benchmark results are claimed.

## Build and test

`run.sh` requires a POSIX shell and C++17 compiler (Apple Clang, Clang, or GCC).
From the repository root:

```sh
./run.sh
```

The script compiles all current source and test files and writes the shared
self-test summary and a foundation heap-event demonstration. That event demo is
illustrative heap order only, **not a schedule**. MSVC C++17 `/W4 /WX`
compilation has also succeeded. The prior test baseline passed 77/77. Five
boundary tests were subsequently added; the updated 82-test suite compiled with
MSVC, but Windows Application Control blocked execution. **Do not report
82/82 passing.**

## Graph and validation module

The public graph representation is an adjacency list backed by a head-index
array and `{to,next}` edge pool. Graph construction sorts edge pairs to reject
duplicates; construction is O(N + E log E), and graph storage is O(N+E).
Successor traversal is O(outdegree) per vertex.

Kahn topological sorting and reverse-order critical-path ranking each take
O(N+E) time and O(N) temporary space. `validate_workload(ParsedInput)` validates
task times and dependencies and returns dense task records, the graph,
topological order, and critical-path ranks. Its overall cost is
O(N + E log E), including graph construction.

`verify_result` independently checks task coverage/indexing, timestamps,
durations, readiness/dependency constraints, same-unit interval overlap,
busy-tick totals, and makespan. It sorts execution intervals, so its time
complexity is O(N log N + E + P) and additional storage is O(N+P), where P is
the compute-unit count. It verifies **feasibility and statistics**; it does not
check scheduling-policy ordering or work conservation.

Deterministic generators return `ParsedInput`:

```cpp
gpu::ParsedInput gpu::make_chain_workload(
    std::size_t task_count, gpu::Tick release = 0, gpu::Tick duration = 1);
gpu::ParsedInput gpu::make_fork_join_workload(
    std::size_t branch_count, gpu::Tick release = 0, gpu::Tick duration = 1);
gpu::ParsedInput gpu::make_layered_workload(
    const std::vector<std::size_t>& layer_widths,
    gpu::Tick release = 0, gpu::Tick duration = 1);
```

They assign dense IDs and deterministic `T0`, `T1`, … names, create
predecessor-to-successor edges, reject invalid timing/count parameters, and can
be passed directly to `validate_workload`. See
[docs/CONTRACTS.md](docs/CONTRACTS.md) for detailed graph shapes and complexity.

## Team responsibilities and integration

| Owner | Assigned structure/module | Current integration status |
| --- | --- | --- |
| Pranav | Binary heap; event engine | Heap foundation present; engine pending |
| Aayush | Circular-array deque; scheduling policies and placement | Deque declaration only; integrated policies pending |
| Shaswitha | Directed graph, DAG algorithms, workload/trace validation, workload generators | Implemented; see [contribution evidence](docs/CONTRIBUTIONS.md) |
| Divyaa | Chained hash table, parser, metrics | Hash table declaration only; parser and metrics pending |

The parser should preserve input-row order, resolve external IDs to dense
`TaskId`s, and produce `ParsedInput` with predecessor-to-successor edges. The
engine consumes `ValidatedWorkload` and produces one `TaskRecord` per task at
its dense index, plus per-unit busy ticks and makespan. Before expanding trace
verification, the team should agree whether it also checks policy ordering,
work conservation, and ownership/stealing semantics.

## Project references

Start with [the team quickstart](docs/QUICKSTART.md),
[shared contracts](docs/CONTRACTS.md), and [the task board](docs/TASKS.md).
Record actual implementation, test, review, and presentation evidence in
[contributions](docs/CONTRIBUTIONS.md). Do not invent commit/PR links or claim
tests that were not executed.
