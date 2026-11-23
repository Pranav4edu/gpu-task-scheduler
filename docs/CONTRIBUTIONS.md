# Final integration update - November 23, 2026

The records below preserve module provenance. Final integration was performed at
Pranav's request with OpenAI Codex assistance; it is not additional independent
work attributed to another student. The integration merge retains Divyaa's commit
`6314ab4`, Aayush's history through `f8057f1`, Shaswitha's `611f365`, and Pranav's
engine `bb73b9d`. The export fix `8fdbcfc` was published to PR #6.

Completed in the final integration: shared test-runner merge, `load_and_validate`,
complete no-argument experiment driver, 36 additional saved main workloads, hash
instrumentation, event/graph/preprocessing evidence, seven full-pipeline tests,
960 production comparisons against the independent tick oracle, CI directory
fix, output audit, final measured report, presentation guide and packaging.

Divyaa's submitted module includes the custom hash table, two-pass parser,
metrics and 16 test groups. Owner understanding, a live presentation and any
course-required personal acknowledgments must be completed by the students;
this document does not assert those occurred. Historical pending statements
below describe their original checkpoints, not the current implementation.

---

# Individual contribution and evidence

This record describes implementation status and available verification evidence;
it does not substitute for course-required acknowledgment of assistance, owner
understanding, peer review, or actual commit/PR records. Do not invent links or
claim independent authorship based only on a task assignment.

## Aayush - ring deque, schedulers, export and owner-skew workloads

All of the following was drafted with AI assistance (Claude Code) at Aayush's request.
This is not a claim of independent student authorship; owner review, explanation and
peer review are still pending and must be recorded by the contributor. Details, design
rationale and per-step checklists are in [AAYUSH_WORK_LOG.md](AAYUSH_WORK_LOG.md).

- `RingDeque<T>` (`code/include/gpu/ds/ring_deque.hpp`, `code/tests/test_ring_deque.cpp`,
  8 tests): circular array, doubling growth, both-end operations, operation counters.
- Production schedulers behind the shared `Scheduler` interface
  (`code/include/gpu/scheduler.hpp`, `code/src/policies.cpp`, `make_scheduler`): central
  FIFO on `RingDeque`, central SJF and CP on the shared `BinaryHeap`, static round-robin
  FIFO (one `RingDeque` per unit) and optional work stealing. Tests: `test_scheduler.cpp`,
  `test_policies.cpp` (including the six-task hand oracles), `test_static_scheduler.cpp`,
  `test_stealing.cpp`.
- Export and counters: results CSV, makespan and busy-time SVG charts, result-file bundle,
  `ds_metrics` counter CSV (`export.hpp`, `ds_metrics.hpp`, `test_export.cpp`,
  `test_ds_metrics.cpp`).
- Owner-skew workload generator and 12 saved fixtures (`workloads.hpp`,
  `code/data/workloads/`, `test_workloads.cpp`).
- Engine integration: `simulate()` wrapper (`code/src/simulate.cpp`) and
  `code/tests/test_scheduler_engine.cpp`, which run all five production configurations
  through Pranav's real engine and Shaswitha's real graph and verifier.
- Handoff notes: [INTEGRATION_AAYUSH.md](INTEGRATION_AAYUSH.md) and
  [AAYUSH_PRESENTATION_NOTES.md](AAYUSH_PRESENTATION_NOTES.md).

Limits: stealing has not been checked by a policy-aware validator (the verifier does not
check ownership or steal flags; the engine does check them). Fixtures were not loaded
through a CSV parser (none exists yet). No measured benchmark results are claimed.

## Shaswitha — graph and validation module

The assigned module implementation includes:

- Custom adjacency-list `DirectedGraph` using per-vertex head indices and a
  `{to,next}` edge pool. Construction validates endpoints and self-edges, sorts
  edge pairs to reject duplicates, and retains original indegrees.
- Kahn topological sorting, including disconnected-cycle detection, and
  reverse-topological critical-path ranks with checked tick addition.
- `validate_workload(ParsedInput)`, which validates task times, constructs the
  graph, rejects cyclic dependencies, and returns dense task records, a
  topological order, and ranks.
- `verify_result`, which independently checks trace feasibility, dependency
  readiness, unit intervals, busy ticks, and makespan. It does **not** check
  scheduling-policy order or work conservation.
- Deterministic chain, fork-join, and layered `ParsedInput` generators.

Implementation files:

- `code/include/gpu/ds/directed_graph.hpp`, `code/src/directed_graph.cpp`
- `code/src/dag_algorithms.hpp`, `code/src/dag_algorithms.cpp`
- `code/src/workload_validation.cpp`, `code/src/result_validation.cpp`
- `code/include/gpu/workload_generators.hpp`,
  `code/src/workload_generators.cpp`
- Focused cases in `code/tests/self_test.cpp`

### Test and build evidence

| Evidence | Status |
| --- | --- |
| Previous suite | 77/77 self-tests passed before the five boundary tests were added. |
| Updated suite | 82 tests are present; the complete source and tests compiled with MSVC C++17 `/W4 /WX`. |
| Updated execution | Windows execution was initially blocked. Subsequently, GitHub CI passed 82/82; the local integration branch also passes 82/82 plus 18 engine/integration groups. |
| PR, commit, peer review | PR #4 at commit 611f365 is included here. Peer review is still pending. |
| Presentation/demo | Explain the graph representation, cycle detection, rank oracle, validation checks, and generator patterns; record the actual rehearsal/review when completed. |

Course-specific attribution and assistance disclosures should be completed by
the contributor. Test output or a successful compile is not a substitute for
review and understanding.

## Other project work

The current source tree still has a declaration-only `IdHashTable` header. The CSV parser,
`load_and_validate` and metrics remain pending. The event engine from PR #3 is included in this
local integration branch. Their owners should add evidence only after implementation
and verification; this document does not assign unverified authorship or review.

## Pranav - event engine


At Pranav's request, Codex implemented `code/src/simulator.cpp`, the non-owning
`DependencyView` adapter, engine tests and the labeled checkpoint output. This is
AI-assisted implementation, not a claim of independent student authorship.
Evidence: `code/tests/test_simulator.cpp` contains 14 test groups, including 960
seeded cases compared with a separate tick-based oracle. `docs/EVENT_ENGINE.md`
explains the algorithm, complexity, test boundaries and remaining integration.
The graph and scheduler substitutes are test fixtures, not contributions to
Shaswitha's or Aayush's custom data structure implementations.

## November 14 integration verification

The Windows execution limitation above is historical. PR #4 subsequently passed
82/82 tests in GitHub CI. This branch combines PR #3 (`bb73b9d`) and PR #4
(`611f365`) for local graph/engine compatibility testing. See INTEGRATION_CHECK.md
for the combined evidence. No peer review or main-branch merge is implied.
