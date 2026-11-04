# Contribution evidence

The initial foundation is AI-assisted setup requested by Pranav. It supplies the
heap starter, shared contracts, tests, fixtures, build automation and docs. This
record does not claim that any member independently authored or completed those
items. Owners should inspect, test, understand and develop their assigned work,
and follow course requirements for acknowledging assistance.

| Member | Assigned DS | Implementation evidence | Review evidence | Explanation/demo |
| --- | --- | --- | --- | --- |
| Pranav | Binary heap | Heap foundation merged in PR #1; AI-assisted engine and isolated tests added November 4 | Owner and peer review pending | Engine trace and walkthrough supplied; student explanation pending |
| Aayush | Circular deque | Pending | Pending | Pending |
| Shaswitha | Adjacency-list graph | Pending | Pending | Pending |
| Divyaa | Chained hash table | Pending | Pending | Pending |

Add actual PR/commit links, test cases and reviews as work lands. Do not measure
contributions by commit count or relabel anticipated work as completed evidence.

## November 4 engine checkpoint

At Pranav's request, Codex implemented `code/src/simulator.cpp`, the non-owning
`DependencyView` adapter, engine tests and the labeled checkpoint output. This is
AI-assisted implementation, not a claim of independent student authorship.
Evidence: `code/tests/test_simulator.cpp` contains 14 test groups, including 960
seeded cases compared with a separate tick-based oracle. `docs/EVENT_ENGINE.md`
explains the algorithm, complexity, test boundaries and remaining integration.
The graph and scheduler substitutes are test fixtures, not contributions to
Shaswitha's or Aayush's custom data structure implementations.
