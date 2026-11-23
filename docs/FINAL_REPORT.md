# GPU Task Scheduling and Load Balancing

Final implementation, measured results and reproducible submission

CE 6320 - Applied Data Structures | Group 4 | November 23, 2026

| Team member | Owned custom data structure |
| --- | --- |
| Pranav Sathyabodha Kumsi | Binary heap and discrete-event engine |
| Aayush Agrawal | Circular-array deque and scheduling |
| Shaswitha Fernandez | Adjacency-list directed graph and DAG algorithms |
| Divyaa Natarajan | Separate-chaining hash table, parser and metrics |


### Project outcome

A self-contained C++17 program now loads CSV task graphs, validates dependencies, schedules tasks on virtual compute units, verifies every trace, calculates metrics and exports CSV/SVG results. The root run.sh builds from source and runs everything without arguments, downloads or manual interaction.

| Verification | Measured evidence |
| --- | --- |
| Custom structures | 4 implemented and used in the production pipeline |
| Automated tests | 202 passing groups: 184 module/pipeline + 18 engine |
| Benchmark suite | 48 saved inputs; 756 independently verified runs |
| Demonstration | Five additional six-task runs with hand-calculated answers |

This is a GPU-inspired simulation on identical virtual compute units. It does not execute CUDA kernels or measure GPU hardware throughput. Integration, added tests, instrumentation and final artifacts were prepared with OpenAI Codex assistance at Pranav's request. Original module histories are retained; each student must still understand and present their contribution.


## 1. Aim, scope and architecture

The project separates two decisions: which eligible task runs next, and which compute unit executes it. A task has an external identifier, a nonnegative release time, a positive duration and zero or more predecessors. Execution is non-preemptive. A task becomes eligible only when it is released and every predecessor has finished.

| Stage | Implementation and output |
| --- | --- |
| CSV input | Two-pass parsing resolves names to dense task IDs through IdHashTable. |
| DAG preprocessing | DirectedGraph stores successors and indegrees; Kahn rejects cycles; reverse topological traversal computes critical-path ranks. |
| Scheduling and execution | BinaryHeap orders arrivals/completions. The scheduler uses RingDeque for FIFO or BinaryHeap for SJF/CP and returns task/unit assignments. |
| Independent verification | Reconstruct eligibility, check exact durations, nonoverlapping unit intervals, task coverage and busy/makespan totals. |
| Metrics and artifacts | Calculate waiting time, turnaround, utilization and imbalance; export charts, traces, counters and reproducibility metadata. |


### Supported policies

Central FIFO preserves readiness order. Central SJF selects the shortest duration. Central CP selects the longest remaining dependency-chain duration. Static FIFO assigns each task to TaskId modulo P and maintains one queue per unit. The optional work-stealing extension first allows every idle owner to dispatch, then probes victims cyclically and steals from the back of a nonempty queue.


### Data ownership

The validated workload owns immutable tasks, graph, topological order and ranks. Each simulation creates fresh runtime state, predecessor counters, queues and statistics. The engine owns time and task lifecycle; the scheduler owns ready-queue membership. Metrics and exporters consume completed results without changing the schedule.

Production entry points: parse_csv, load_and_validate, make_scheduler, simulate_with_scheduler, verify_result, compute_metrics and run_experiments. The ordinary central-FIFO path exercises all four assigned data structures.


## 2. Heap and deque: representation and invariants


### Pranav: custom binary heap

BinaryHeap<T, Before> stores records in a vector. For child index i, its parent is (i-1)/2. Insertion appends a value and sifts it upward while it precedes its parent. Removal copies the root, moves the last value to the root and sifts downward toward the better child. The invariant is that no child precedes its parent. The event comparator includes deterministic timestamp and event tie fields; SJF and CP use immutable ReadyKey records.

Push and pop take O(log H) ordering work for H elements; a vector growth insertion may additionally copy O(H) elements. Top is O(1). Storage is O(H). Tests include empty access, growth with an aliased input, mixed reference operations, duplicate priorities and comparator ties.


### Aayush: custom circular deque

RingDeque stores an array, head index and element count. Logical position i maps to (head+i) modulo capacity. Front/back operations move indices instead of shifting all records. A full queue doubles storage and copies live records in logical order. Empty access throws. Pops are O(1); pushes are amortized O(1), with O(Q) growth for Q live elements. Storage is O(Q).


### Measured container work

| N | Deque push/pop | Growths | Copied | Event push/pop |
| --- | --- | --- | --- | --- |
| 100 | 100/100 | 4 | 120 | 200/200 |
| 1000 | 1000/1000 | 7 | 1016 | 2000/2000 |
| 10000 | 10000/10000 | 11 | 16376 | 20000/20000 |

Source: ds_metrics.csv, central FIFO on owner-skew inputs at P=4. Event counts are 2N because each task generates one arrival and one completion. Counts are operations, not virtual ticks or host seconds. Growth copies provide direct evidence of amortized deque behavior.


## 3. Hash table and graph: names become dependencies


### Divyaa: separate-chaining task lookup

IdHashTable computes FNV-1a over unsigned bytes. Bucket heads point into a vector-backed entry pool; each entry contains key, dense value and next index. Insert searches its collision chain and preserves the original mapping on duplicates. Find returns an optional value. Before exceeding load factor 0.75, rehashing doubles bucket storage and relinks entries. Forced-collision tests use an injected hasher and compare behavior with a test-only unordered_map.

With well-distributed hashes and bounded key lengths, lookup/insertion are expected O(1); a collision chain can make them O(N). Hashing itself costs O(key length). A rehash scans existing entries and hashes their keys. The parser reserves the ID table before registration, so its allocation rehash can occur while it contains zero entries; this is visible rather than hidden in the counters.

| Chain N | ID inserts/finds | ID comparisons | Edges | Pair rehashes |
| --- | --- | --- | --- | --- |
| 100 | 100/99 | 123 | 99 | 5 |
| 1000 | 1000/999 | 1367 | 999 | 8 |
| 10000 | 10000/9999 | 14835 | 9999 | 11 |


### Shaswitha: adjacency-list DAG

DirectedGraph uses a head array and an edge pool of {to,next}, plus original indegrees. Edges point from predecessor to successor. Construction validates endpoints/self-edges and sorts input pairs to reject duplicates; it is O(N + E log E). Storage and successor traversal are O(N+E). Kahn's algorithm copies indegrees, emits zero-indegree vertices and rejects a cycle when fewer than N vertices are emitted.

Critical-path rank is duration(task) + maximum successor rank, or duration for a sink. Reverse topological evaluation costs O(N+E). Checked arithmetic rejects rank overflow. The engine never mutates the original indegrees, allowing repeated policies to use the same validated DAG.


## 4. Event semantics and scheduling walkthrough

The event engine initially schedules one release event per task. At the next event time it processes all completions, frees units and decrements successor counters. It then processes arrivals, collects newly eligible tasks once, sorts the new batch by dense ID and enqueues it. Idle units are offered in ascending ID order. Each assignment records start/finish and schedules one completion event.

Simultaneous completion and release permit immediate start. Tasks already waiting retain their FIFO position ahead of the new batch. SJF breaks duration ties by ready time then task ID; CP breaks descending-rank ties the same way. Static ownership is fixed even while a task is blocked. Stealing never migrates a running task.


### Six-task example, P=2

A(2), B(3) and C(6) are roots. D(2) depends on A and B; E(4) depends on C; F(1) depends on D and E. All release times are zero. Critical ranks are A=5, B=6, C=11, D=3, E=5 and F=1.

| Task | FIFO start-finish / unit | CP start-finish / unit |
| --- | --- | --- |
| A | 0-2 / 0 | 3-5 / 1 |
| B | 0-3 / 1 | 0-3 / 1 |
| C | 2-8 / 0 | 0-6 / 0 |
| D | 3-5 / 1 | 5-7 / 1 |
| E | 8-12 / 0 | 6-10 / 0 |
| F | 12-13 / 0 | 10-11 / 0 |

FIFO and SJF finish at tick 13; CP finishes at tick 11 by starting the longer dependency chain earlier. Static FIFO and stealing also finish at 13 on this example. A policy improving makespan need not improve mean ready wait: CP has mean wait 0.5 here versus 0.333333 for FIFO.

Source: code/data/fixtures/six_tasks.csv and generated trace.csv. These task-level results match the separately saved hand oracle, not merely another call to the same simulation.


## 5. Metrics and what they mean

| Metric | Definition and interpretation |
| --- | --- |
| Ready time | max(task release, latest predecessor finish). Dependency waiting is not ready-queue waiting. |
| Ready waiting time | start - ready; report mean and nearest-rank p95. The p95 rank is N - floor(N/20), one-based. |
| Turnaround | finish - release; mean includes waiting for prerequisites and service. |
| Makespan | Maximum finish time, measured from tick zero. Empty workload: zero. |
| Useful utilization | Sum of task durations / (P x makespan); includes release-related idle time. |
| Busy-time CV | Population standard deviation of per-unit busy ticks divided by their mean. Lower means more even total busy time, not necessarily more overlap or better makespan. |


### Measured six-task metrics

| Configuration | Makespan | Mean wait | Utilization | Busy CV |
| --- | --- | --- | --- | --- |
| central_fifo | 13 | 0.333 | 69.23% | 0.444 |
| central_sjf | 13 | 0.333 | 69.23% | 0.444 |
| central_cp | 11 | 0.500 | 81.82% | 0.222 |
| static_fifo | 13 | 0.333 | 69.23% | 0.333 |
| stealing_fifo | 13 | 0.333 | 69.23% | 0.333 |

Empty averages and ratios with zero denominators are exported as N/A. Metrics use long-double accumulators for means; tick arithmetic is checked where totals can overflow. The result verifier runs before production metrics, so feasible task records and consistent unit totals are prerequisites.


### A useful counterexample

At N=10,000 and P=8, a chain takes 30,000 ticks under every policy and utilizes only 12.5% of aggregate capacity. Static ownership distributes total work equally (CV=0), yet dependencies still allow only one running task. A low CV alone is not proof of speedup.


## 6. Reproducible experiment design

| Family | Instances | Definition |
| --- | --- | --- |
| Chain | 3 sizes | N tasks; duration 3; release 0; each depends on the previous task. |
| Fork-join | 3 sizes | One root, N-2 parallel branches, one sink; duration 3 and release 0. |
| Mixed independent | 3 sizes x 5 seeds | Durations 1..100, no edges, deterministic staggered releases. |
| Layered DAG | 3 sizes x 5 seeds | Width 10; up to three unique predecessors from the preceding layer; mixed durations/releases. |
| Owner skew | 3 sizes x 4 P values | TaskId % P = 0 gets duration 10; other tasks duration 1. Used only for within-P comparisons. |

Sizes are 100, 1,000 and 10,000. The 36 main inputs are reused at P=1,2,4,8 with all five configurations: 36 x 4 x 5 = 720 runs. The 12 skew inputs use central/static/stealing FIFO only: 12 x 3 = 36 runs. Total: 756, plus five small demonstration runs kept in separate files.


### Seeds and saved inputs

Mixed and layered seeds are 101, 202, 303, 404 and 505. The generator uses specified unsigned 32-bit arithmetic: state = 1664525*state + 1013904223. Duration is 1 + state modulo 100. Releases are ((task index / 25) modulo 4)*20. Layered predecessor selection uses state modulo 10, discarding duplicate choices. This is a bounded synthetic test design, not a model of production GPU arrivals.

All workload CSVs are saved before comparing policies. Tests reproduce them byte-for-byte. The runtime records filenames, seeds, N, E, P, policy, compiler, flags, host, source version and input CRC checksums. Virtual metrics need one deterministic run per configuration; no host-runtime performance claims or statistical-significance claims are made.

Settings: code/config/README.md. Inputs: code/data/workloads/. Actual run matrix: experiment_manifest.csv. The former planning report's required 600-run baseline is fully covered; optional stealing extends it to 756.


## 7. Results: makespan and distribution

| N=10,000; P=8 | FIFO | SJF | CP | Static | Steal |
| --- | --- | --- | --- | --- | --- |
| chain | 30,000 | 30,000 | 30,000 | 30,000 | 30,000 |
| fork_join | 3,756 | 3,756 | 3,756 | 3,756 | 3,756 |
| mixed S101 | 62,451 | 62,451 | 62,411 | 63,808 | 62,450 |
| layered S101 | 81,099 | 82,484 | 79,663 | 101,124 | 81,171 |
| owner skew | 2,662 | not run | not run | 12,500 | 2,660 |

The chain cannot exploit multiple units because of dependencies. Fork-join approaches full utilization once branches become ready. The mixed and layered rows are representative seed 101 cases; all five seeds are preserved in the raw metrics table. No universal policy winner follows from these examples.


### Load balancing on deliberately skewed ownership

Owner skew, N=10,000, P=8 - makespan (ticks): Central FIFO=2662, Static FIFO=12500, Stealing FIFO=2660

Static FIFO takes 12,500 ticks because unit 0 owns all heavy work. Central FIFO takes 2,662; stealing takes 2,660. Relative to static, these reduce makespan by 78.70% and 78.72% respectively. This is a constructed stress case, not an average speedup claim.

Source: metrics.csv. Compare distributions at the same P; changing P also changes which tasks are heavy in the skew family.


## 8. Results: policy tradeoffs across seeds


### Five-seed paired results, N=10,000 and P=8

| Family | Compared with FIFO | Mean makespan change | Range | Mean wait change |
| --- | --- | --- | --- | --- |
| mixed | central_sjf | +0.00% | -0.00 to +0.01% | -33.21% |
| mixed | central_cp | -0.07% | -0.08 to -0.06% | +33.00% |
| mixed | static_fifo | +3.41% | +2.17 to +5.05% | +0.04% |
| mixed | stealing_fifo | -0.01% | -0.03 to +0.00% | -0.02% |
| layered | central_sjf | +1.56% | +1.32 to +1.78% | -20.27% |
| layered | central_cp | -1.80% | -2.09 to -1.63% | +28.67% |
| layered | static_fifo | +24.23% | +23.43 to +24.69% | +1198.50% |
| layered | stealing_fifo | +0.05% | -0.07 to +0.18% | +0.20% |

Each change is paired against central FIFO on exactly the same saved input and P. Negative values mean a reduction; the table averages the five per-seed percentages. The range shows variation across these fixed synthetic cases, not a confidence interval.


### Interpreting the tradeoff

On independent tasks CP reduces to prioritizing longer durations because there are no successor chains. This can reduce the final long-task tail while increasing how long small tasks wait. SJF prioritizes short work and can reduce average ready wait while leaving a long tail. Layered dependencies change which tasks are available, so precomputed critical ranks can expose future work earlier.

Load imbalance and makespan must be interpreted together. A scheduler can keep work evenly distributed but still delay a critical task; it can also show uneven busy totals because the workload itself is serial. The independent verifier guarantees feasibility, while policy tests and reference comparisons establish deterministic choice semantics.


## 9. Scaling with more virtual compute units

| N=10,000 input | P=1 | P=2 | P=4 | P=8 | P=8 speedup |
| --- | --- | --- | --- | --- | --- |
| chain | 30,000 | 30,000 | 30,000 | 30,000 | 1.00x |
| fork_join | 30,000 | 15,003 | 7,506 | 3,756 | 7.99x |
| mixed S101 | 499,284 | 249,646 | 124,847 | 62,451 | 7.99x |
| layered S101 | 505,971 | 252,997 | 127,472 | 81,099 | 6.24x |

Speedup here means P=1 makespan divided by P=8 makespan under central FIFO on the same input. It is simulated parallelism, not measured acceleration on a GPU or across host threads. No skew workload is included because its duration pattern changes with P.


### Checks against simple mathematical results

For a chain of N tasks of duration 3 and zero release, every policy must finish at 3N regardless of P. For central policies on the uniform fork-join input, makespan is 6 + 3*ceil((N-2)/P): the root and sink are serial and branches occupy the middle waves. For owner-skew static FIFO, makespan is 10*ceil(N/P). The output audit asserts these formulas for all corresponding benchmark rows.


### What the results do not establish

The workload set does not characterize real kernels, memory transfer, cache behavior, occupancy, launch overhead or heterogeneous processors. Policies know task duration and dependency ranks offline. The simulator has zero dispatch overhead and uses one host thread. Increasing virtual P does not create actual host parallelism. The implementation is designed for explainable data-structure comparisons rather than a hardware scheduler claim.

Every schedule also conserves useful work: summed busy ticks equal summed durations. Utilization never exceeds 1 for a verified schedule. These independent constraints catch errors that a plausible-looking makespan chart could conceal.


## 10. Correctness and validation evidence

| Layer | Evidence |
| --- | --- |
| Data structures | Heap reference operations and invariants; deque wraparound/growth; hash forced collisions, rehash and reference-map tests; graph edge and cycle validation. |
| Algorithms | Known six-task traces/ranks; simultaneous events, idle gaps, overflow, empty input, invalid scheduler assignments and repeated runs. |
| Differential checks | 960 randomized cases each for fixture graphs, real graphs with a reference scheduler, and real graphs with production schedulers; all match the independent tick oracle. |
| Full file pipeline | Seven added groups cover CSV-to-export, forward references, multiple predecessors with competing releases, empty input, rejection paths, counters and saved input reproduction. |
| Benchmark validation | 756 complete runs pass the independent trace verifier; five six-task examples also verified. |
| Independent output audit | Python rechecks metric formulas, known family makespans, dimensions, event/edge counts, small traces and SVG XML. Python is optional for grading. |

The full C++ suite passes 184 module/pipeline groups plus 18 engine groups, totaling 202. A group may contain many cases; 202 is not the number of individual assertions. The added production differential checks address a former gap where the event-engine randomized suite used only reference schedulers.

GitHub runs the no-argument build and experiment pipeline on Linux and macOS, runs the independent output audit, and runs AddressSanitizer plus UndefinedBehaviorSanitizer tests on Linux. A prior sanitizer failure was traced to the working directory: fixture paths are relative to code/. The workflow now enters that directory before running the sanitized executable.

Reproducibility gate: extract Group4_Project.zip into a new path containing spaces, invoke run.sh from outside the project, and compare generated metrics/charts with the reviewed outputs. See the delivered SUBMISSION_CHECK.txt for the actual final-package check. CI evidence is linked from the final integration PR.


## 11. Complexity and optimizations

| Component | Time | Additional/storage cost |
| --- | --- | --- |
| ID hashing / parsing | Expected linear in input text and lookup work; collision-heavy worst case can be quadratic in key count. | O(input text + N + E); parser retains lines and pending dependency names. |
| Graph construction | O(N + E log E), including edge sort for duplicate rejection. | O(N+E) graph storage; by-value input edge vector sorted in place. |
| Kahn and CP ranks | O(N+E) each, including linear validation traversals. | O(N) metadata and work queues beyond graph. |
| Engine, required policies | O(N log N + E + NP); heap/ready sorting and idle-unit scans included. | O(N+E+P) for one workload/run and queues. |
| Optional work stealing | Adds up to O(NP squared) victim scanning in a pessimistic event-by-event bound. | O(N+P) ready-queue storage. |
| Trace verification | O(N log N + E + P) because unit intervals are sorted. | O(N+P) temporary verification data. |
| Metrics | O(N log N + P), dominated by sorting wait values for p95. | O(N) wait vector. |


### Implemented optimizations

The engine jumps directly between event timestamps rather than simulating every tick. Completion processing touches successors rather than rescanning the whole graph. Critical ranks are computed once per input; task IDs are compact indices. The parser reserves its ID table, and FIFO queues grow geometrically. Benchmark output suppresses per-task traces for large inputs while still retaining records temporarily for independent verification.


### Limits of the cost model

String length and allocation affect preprocessing. P is explicit in the engine bound because idle units are scanned; optional stealing performs bounded cyclic victim probes. Full-suite exporting retains aggregate metric/counter rows across runs and writes many charts; these output costs are separate from the single-run engine bound. Operation counters quantify specific container calls and traversals, not universal performance.


## 12. Contributions and integration ownership

| Owner | Module evidence | Explanation responsibility |
| --- | --- | --- |
| Pranav | Custom heap, event engine, shared executable/run script; final integration requested and reviewed with AI assistance. | Heap ordering, simultaneous events, state transitions, deterministic run and pipeline. |
| Aayush | RingDeque, FIFO/SJF/CP/static/stealing schedulers, exporters, scheduler counters and owner-skew inputs. | Wraparound/growth, queue policies, ownership/stealing and result charts. |
| Shaswitha | DirectedGraph, Kahn cycle checks, CP ranks, DAG generators, workload and trace validators. | Adjacency representation, eligibility metadata, DAG invariants and independent verification. |
| Divyaa | IdHashTable, two-pass CSV parser, metrics and 16 module test groups. | Hashing/collisions/rehashing, forward references, metric formulas and edge cases. |

The integration preserves contributor commit history rather than copying code under invented authorship. Key source snapshots include Pranav's bb73b9d, Shaswitha's 611f365, Aayush's f8057f1, Divyaa's 6314ab4 and the export fix 8fdbcfc. Final integration adds the load_and_validate bridge, test registration, real experiment driver, input generation, counters, regressions, CI fix, report and package.

Existing contribution notes identify AI-assisted work. This report does not claim that generating code establishes student mastery or independent authorship. Students remain responsible for course-required disclosure, reviewing their code and explaining it in the presentation. Commit count is not used as a measure of contribution difficulty.


### Presentation plan

Use an eight-minute walkthrough: Divyaa on hashing/parsing/metrics (1.5 min), Shaswitha on the DAG and verifier (1.5 min), Pranav on heap/events (2 min), Aayush on policies/results (2 min), then one minute shared for reproducibility and limitations. docs/PRESENTATION_GUIDE.md contains code locations and practice questions.


## 13. Reproduction, submission and conclusion


### Grading entry point

Extract Group4_Project.zip, then execute the root run.sh without arguments. The script compiles C++17 sources and tests with warnings treated as errors, runs tests, runs the experiment matrix, verifies every schedule and writes fresh code/results outputs. A POSIX shell and installed C++17 compiler are the only execution prerequisites. There are no downloaded dependencies, precompiled executables, Windows batch files or absolute input paths.

| Package item | Purpose |
| --- | --- |
| Group4_Final_Report.pdf | This measured final report. |
| README.md and run.sh | Directory/run instructions and automated grading entry point. |
| code/include, src, tests | All four implementations, algorithms and tests. |
| code/data and config | Saved workload inputs, hand oracles and generator settings. |
| docs/ and tools/ | Contracts, provenance, presentation guide and optional audits. |
| SOURCE_VERSION.txt | Source snapshot used to build the submission. |

Generated binaries are excluded. code/results is regenerated by run.sh. The packaged report is static, while its companion CSV tables and charts can be recreated from the exact included inputs. The optional report builder requires ReportLab only when editing the report; it is not invoked by grading.


### Findings

The final implementation demonstrates how four custom structures cooperate in one scheduling pipeline. Dependency shape sets a ceiling on parallelism; task selection affects ready waiting and critical-chain progress; placement can create severe imbalance under skew. Central scheduling and optional stealing recover much of the deliberately skewed static loss, while chains remain serial. The results support these bounded conclusions on saved synthetic inputs rather than a universal or hardware performance claim.


### Evidence locations

Repository: https://github.com/Pranav4edu/gpu-task-scheduler. Contracts and module paths are documented in docs/CONTRACTS.md and docs/FINAL_INTEGRATION.md. Metric formulas, experiments and measured tables in this report are derived from the included implementation and generated CSV evidence. Earlier proposal/progress reports describe plans and are superseded by this implementation report.

Before submission, each member should rehearse their module and confirm the LMS submission cutoff. Uploading to the course portal and giving the presentation are student actions; they are not represented as completed by this report.
