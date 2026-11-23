# Implementation board

Status at the November 14 local graph/engine integration check. Dates are November 2026. An implemented
starter still requires owner review and understanding before being credited as
that student's independently completed work.

| Deliverable | Owner | Due | Current status |
| --- | --- | --- | --- |
| Shared APIs and event/CSV rules | All; Pranav coordinates | 1 | Foundation merged; additive engine adapter ready for consumer review |
| Custom binary heap and tests | Pranav | 4 | AI-assisted starter implemented; owner review pending |
| Custom ring deque and tests | Aayush | 4 | AI-assisted implementation, 8 unit tests; used by the production schedulers inside the real engine; owner review pending |
| Custom adjacency graph and tests | Shaswitha | 4 | PR #4 implementation included; connected to engine in this branch |
| Custom hash table and tests | Divyaa | 4 | Header contract only |
| Parser plus DAG validation/ranks | Divyaa + Shaswitha | 6 | DAG validation/ranks implemented; CSV parsing pending |
| Event engine in isolation | Pranav | 4 | Implemented; 14 test groups including 960 differential cases; review pending |
| Engine plus centralized FIFO | Pranav + Aayush | 8 | Real graph, real engine and Aayush's production central FIFO scheduler run together and pass the verifier (integration tests); owner reviews pending |
| Baseline using all four custom structures | All | 8 | Pending |
| Central SJF/CP integration | Aayush + Pranav | 12 | Production SJF/CP schedulers reproduce the six-task hand traces (13 / 13 / 11) in the real engine; owner reviews pending |
| Independent trace validator | Shaswitha | 12 | Implemented; checks real graph/engine results here |
| Metrics and hand-oracle comparison | Divyaa | 12 | Pending |
| Static queues, saved workloads and basic charts | Shared | 16 | Aayush's static FIFO scheduler, factory and chart/CSV export and owner-skew fixtures done (AI-assisted, review pending); other workload families pending |
| Optional stealing decision | Aayush + Pranav | 16 | Stealing scheduler implemented and unit-tested (AI-assisted, review pending); inclusion decision waits for engine trace validation |
| Required implementation merged | All | 20 | Pending |
| Full integration and first extraction check | All | 23 | Pending |
| Measured report and presentation rehearsal | All | 24-29 | Pending |

## Immediate next commits

1. Each member reviews their API and asks consumers about changes.
2. Aayush reviews his deque/scheduler/export code and the engine integration tests (see
   INTEGRATION_AAYUSH.md); the production `simulate()` wrapper now exists.
3. Shaswitha reviews the graph handoff and independent verifier scope.
4. Divyaa implements IdHashTable with forced-collision tests.
5. Pranav reviews the combined graph/engine/scheduler results. INTEGRATION_CHECK.md
   has the graph/engine evidence; tests/test_scheduler_engine.cpp has the scheduler evidence.

No one should wait for invitations to the remaining members before doing their
independent module. Avoid finishing another owner's entire data structure without
agreeing on how implementation, review and learning responsibilities will change.
