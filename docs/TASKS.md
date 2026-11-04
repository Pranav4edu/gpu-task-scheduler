# Implementation board

Status at the November 4 event-engine checkpoint. Dates are November 2026. An implemented
starter still requires owner review and understanding before being credited as
that student's independently completed work.

| Deliverable | Owner | Due | Current status |
| --- | --- | --- | --- |
| Shared APIs and event/CSV rules | All; Pranav coordinates | 1 | Foundation merged; additive engine adapter ready for consumer review |
| Custom binary heap and tests | Pranav | 4 | AI-assisted starter implemented; owner review pending |
| Custom ring deque and tests | Aayush | 4 | Header contract only |
| Custom adjacency graph and tests | Shaswitha | 4 | Header contract only |
| Custom hash table and tests | Divyaa | 4 | Header contract only |
| Parser plus DAG validation/ranks | Divyaa + Shaswitha | 6 | Pending; fixtures supplied |
| Event engine in isolation | Pranav | 4 | Implemented; 14 test groups including 960 differential cases; review pending |
| Engine plus centralized FIFO | Pranav + Aayush | 8 | Engine ready; real graph/scheduler integration pending |
| Baseline using all four custom structures | All | 8 | Pending |
| Central SJF/CP integration | Aayush + Pranav | 12 | Heap keys and engine hand traces tested; production scheduler pending |
| Independent trace validator | Shaswitha | 12 | Pending |
| Metrics and hand-oracle comparison | Divyaa | 12 | Pending |
| Static queues, saved workloads and basic charts | Shared | 16 | Pending |
| Optional stealing decision | Aayush + Pranav | 16 | Deferred until required baseline passes |
| Required implementation merged | All | 20 | Pending |
| Full integration and first extraction check | All | 23 | Pending |
| Measured report and presentation rehearsal | All | 24-29 | Pending |

## Immediate next commits

1. Each member reviews their API and asks consumers about changes.
2. Aayush implements RingDeque and unit tests; no parser/graph dependency needed.
3. Shaswitha implements graph construction from in-memory edge pairs.
4. Divyaa implements IdHashTable with forced-collision tests.
5. Pranav reviews and explains the heap/engine, then connects the real graph and
   scheduler when ready. See EVENT_ENGINE.md for the adapter and acceptance gates.

No one should wait for invitations to the remaining members before doing their
independent module. Avoid finishing another owner's entire data structure without
agreeing on how implementation, review and learning responsibilities will change.
