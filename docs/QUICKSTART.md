# Joining and integrating the project

1. Accept the collaborator invitation with your own GitHub account.
2. Clone the repository and enter it:

   ```sh
   git clone https://github.com/Pranav4edu/gpu-task-scheduler.git
   cd gpu-task-scheduler
   ./run.sh
   ```

3. Configure your own commit name and a verified GitHub email or your GitHub
   noreply address. Never use another member's identity.
4. Read [CONTRACTS.md](CONTRACTS.md) and [TASKS.md](TASKS.md), coordinate API
   changes with affected owners, and create a feature branch from the current
   team base.
5. Implement only your assigned pending module, add focused tests to
   `code/tests/self_test.cpp`, and run the project's supported build/test
   workflow before pushing.
6. Open a pull request with actual test evidence and a small example. Request
   another member's review and resolve interface changes together.

`run.sh` requires a POSIX shell and a C++17 compiler (Clang or GCC); it is not a
PowerShell script. The project has also been compiled with MSVC C++17 `/W4
/WX`. The updated test executable was blocked by Windows Application Control,
so do not claim the updated 82-test suite passed at runtime.

## Implemented graph module

Shaswitha's graph and validation module currently provides:

- `DirectedGraph::build(vertices, edges)` with adjacency-list edge-pool storage,
  endpoint/self-edge checks, and duplicate-edge checks.
- `detail::topological_order` using Kahn's algorithm and
  `detail::critical_path_ranks` using reverse topological order.
- `validate_workload(ParsedInput)` to validate task times and DAG structure and
  return graph metadata.
- `verify_result(workload, config, result)` for independent trace feasibility
  and statistics checks.
- `make_chain_workload`, `make_fork_join_workload`, and `make_layered_workload`
  in `gpu/workload_generators.hpp`.

The generators return deterministic `ParsedInput` values and can be passed
directly to `validate_workload`. Their exact signatures, shapes, exception
conditions, and module complexity are in [CONTRACTS.md](CONTRACTS.md).

`verify_result` checks task records, times, dependencies, per-unit interval
overlap, busy ticks, and makespan. It does not validate scheduling-policy order
or work conservation; ownership and stealing semantics also require team
agreement.

## Current project status and test evidence

The graph, DAG algorithms, workload validation, trace feasibility validator,
and deterministic workload generators are implemented. The previous suite
passed 77/77. Five boundary tests were added afterward, and all 82 tests
compiled with MSVC C++17 `/W4 /WX`; Windows Application Control blocked
execution. Therefore, 82/82 passing is **not** established.

The ring deque and hash table are still declaration-only. The CSV parser,
`load_and_validate`, event simulator, integrated scheduling policies, metrics,
and full experiment pipeline are pending. The heap event demo is illustrative
only; it is not a dependency-aware schedule or benchmark.

## First integration steps

- Parser owner: resolve external task IDs to dense `TaskId`s in input-row order
  and emit predecessor-to-successor `EdgePair`s in `ParsedInput`.
- Engine/scheduler owners: consume `ValidatedWorkload`, preserve graph
  indegrees, and emit `RunResult` records indexed by task ID.
- Validator/metrics consumers: use `verify_result` before metrics and agree
  whether future verification should enforce work conservation, policy order,
  or ownership/stealing rules.
- Contributors: add real commit, PR, review, and presentation evidence to
  [CONTRIBUTIONS.md](CONTRIBUTIONS.md); do not fabricate links or claim
  unverified test results.
