# Contribution evidence

The initial foundation is AI-assisted setup requested by Pranav. It supplies the
heap starter, shared contracts, tests, fixtures, build automation and docs. This
record does not claim that any member independently authored or completed those
items. Owners should inspect, test, understand and develop their assigned work,
and follow course requirements for acknowledging assistance.

| Member | Assigned DS | Implementation evidence | Review evidence | Explanation/demo |
| --- | --- | --- | --- | --- |
| Pranav | Binary heap | Starter present; review and subsequent work pending | Pending | Pending |
| Aayush | Circular deque | `ring_deque.hpp`, `tests/test_ring_deque.cpp` (8 tests); central FIFO scheduler `scheduler.hpp`, `src/policies.cpp`, `tests/test_scheduler.cpp` (8 tests); SJF and CP schedulers in `scheduler.hpp` with `tests/test_policies.cpp` (12 tests incl. oracles); static round-robin scheduler and `make_scheduler` factory with `tests/test_static_scheduler.cpp` (10 tests); result export (`export.hpp`, `src/export.cpp`, `tests/test_export.cpp`, 10 tests); owner-skew workload generator and 12 saved fixtures (`workloads.hpp`, `src/workloads.cpp`, `tests/test_workloads.cpp`, `data/workloads/`, 5 tests); work-stealing scheduler (`scheduler.hpp`, `src/policies.cpp`, `tests/test_stealing.cpp`, 11 tests; not yet engine-validated); data-structure counter export (`ds_metrics.hpp`, `src/ds_metrics.cpp`, `tests/test_ds_metrics.cpp`, 6 tests); result-file bundle and handoff notes (`docs/INTEGRATION_AAYUSH.md`, `docs/AAYUSH_PRESENTATION_NOTES.md`). All drafted with AI assistance (Claude Code) at Aayush's request; see [AAYUSH_WORK_LOG.md](AAYUSH_WORK_LOG.md) | Pending (owner review not yet done) | Pending |
| Shaswitha | Adjacency-list graph | Pending | Pending | Pending |
| Divyaa | Chained hash table | Pending | Pending | Pending |

Add actual PR/commit links, test cases and reviews as work lands. Do not measure
contributions by commit count or relabel anticipated work as completed evidence.
