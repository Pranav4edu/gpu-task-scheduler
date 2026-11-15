# Implementation board

Status snapshot: November 14, 2026. This board distinguishes implemented
modules from the still-pending full simulator. Update statuses from actual source
and test evidence; do not infer completion, review, or merge status from an
assignment.

| Deliverable | Owner(s) | Current status |
| --- | --- | --- |
| Shared types and agreed module interfaces | All; Pranav coordinates | Present in `code/include/gpu/`; no shared-contract changes are part of the graph module contribution. |
| Binary heap and priority-key foundations | Pranav | Implemented and exercised by the shared self-tests; engine integration remains pending. |
| Custom ring deque | Aayush | Declaration only; implementation and focused tests pending. |
| Adjacency-list graph, topological order, critical-path ranks | Shaswitha | Implemented; included in the 77-test previously passing baseline. |
| `validate_workload(ParsedInput)` | Shaswitha | Implemented; builds the graph and DAG metadata, and is exercised by self-tests. |
| Independent `verify_result` | Shaswitha | Implemented for trace feasibility and statistics. It does not enforce policy ordering or work conservation. |
| Chain, fork-join, and layered workload generators | Shaswitha | Implemented in `gpu/workload_generators.hpp/.cpp`; generator and validation integration tests are present. |
| Chained ID hash table | Divyaa | Declaration only; implementation and focused tests pending. |
| CSV parser and `load_and_validate` | Divyaa; graph validation by Shaswitha | Parser and loader are pending; `validate_workload` is implemented. |
| Event engine and central FIFO simulation | Pranav, with scheduler integration | Pending. |
| Central FIFO/SJF/critical-path and static FIFO policy integration | Aayush and Pranav | Comparator helpers exist; end-to-end policy scheduling remains pending. |
| Optional work stealing | Aayush and Pranav | Deferred until the required baseline is integrated and passes. |
| Metrics, experiment outputs, charts, and hand-oracle comparison | Divyaa and team | Pending. |
| Full simulator integration and final extraction check | All | Pending parser, engine, scheduler, and metrics integration. |

## Verification snapshot

- The previous self-test suite passed **77/77**.
- Five boundary tests were subsequently added, bringing the source suite to 82
  tests. The updated suite compiled with MSVC C++17 `/W4 /WX`.
- Execution of that updated executable was blocked by Windows Application
  Control. **82/82 has not been verified and must not be claimed.**

See [CONTRIBUTIONS.md](CONTRIBUTIONS.md) for individual module evidence and
review/presentation recordkeeping.

## Integration sequence

1. Implement the parser to preserve input-row order, resolve external IDs to
   dense `TaskId`s, and return predecessor-to-successor `EdgePair`s in
   `ParsedInput`.
2. Implement `load_and_validate` using the parser and existing
   `validate_workload`.
3. Implement the event engine and scheduler against `ValidatedWorkload`; emit
   one `TaskRecord` per dense task ID, per-unit busy ticks, and makespan.
4. Agree whether trace verification will remain a feasibility/statistics check
   or also validate work conservation, policy ordering, and ownership/stealing
   semantics. The current verifier does not enforce those scheduler policies.
5. Implement metrics and experiment outputs, then run end-to-end policy and
   hand-oracle tests in an environment that permits execution of the test
   executable.
