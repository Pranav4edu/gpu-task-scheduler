# Individual contribution and evidence

This record describes implementation status and available verification evidence;
it does not substitute for course-required acknowledgment of assistance, owner
understanding, peer review, or actual commit/PR records. Do not invent links or
claim independent authorship based only on a task assignment.

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

The current source tree still has declaration-only `RingDeque` and
`IdHashTable` headers. The CSV parser, `load_and_validate`, integrated production scheduling policies,
and metrics remain pending. The event engine from PR #3 is included in this
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
