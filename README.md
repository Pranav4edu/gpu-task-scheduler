# GPU Task Scheduler and Load Balancer

CE 6320 Group 4: a deterministic C++17 simulator of dependent tasks on virtual
compute units. No physical GPU or CUDA is required.

## Current checkpoint

**Local graph + engine integration check, November 14; not the final submission.**
This branch combines Pranav's engine (PR #3) with Shaswitha's real adjacency-list
graph, Kahn ordering, CP ranks, workload validator, trace verifier and DAG
generators (PR #4). `simulate_with_scheduler` connects those modules without
changing their existing interfaces. Tests and the checkpoint trace use the real
graph, while scheduler substitutes remain in `code/tests/`. The deque, hash table,
parser, production scheduler, metrics and full experiment pipeline remain pending.
The two-argument `simulate` wrapper still awaits production scheduler construction.
See [the integration record](docs/INTEGRATION_CHECK.md) for scope and evidence.

## Run

Requires a POSIX shell and a C++17 compiler (Apple Clang, Clang or GCC).
From the repository root:

```sh
./run.sh
```

The script resolves its own location, compiles source and tests, stops on failure,
and writes these files without arguments or network downloads:

- `code/results/test_summary.txt`: 82 foundation/graph tests and 18 engine/integration
  groups, including 960 fixture-graph and 960 real-graph comparisons with a tick
  oracle, plus 72 generated-workload/configuration combinations.
- `code/results/heap_event_demo.csv`: illustrative heap ordering, **not a schedule**.
- `code/results/engine_fixture_trace.csv`: a real engine trace for the six-task
  FIFO fixture, using the **real graph and a test-only scheduler**, checked by
  Shaswitha's independent trace verifier; not the CSV pipeline.
- `code/results/engine_checkpoint.txt`: fixture makespan, busy ticks and engine
  counters; not a full benchmark report.
- `code/results/project_status.txt`: implemented and pending functionality.

You may select a compiler with `CXX=clang++ ./run.sh`. GitHub Actions runs this same
entry point on Linux and macOS. The final version must extend the pipeline to
validate workloads, run the full comparisons and generate CSV/SVG results.

## Four students and four data structures

| Owner | Custom structure | Integration responsibility |
| --- | --- | --- |
| Pranav | Binary heap | Events, engine, shared executable and run script |
| Aayush | Circular-array deque | FIFO queues, policy selection, static placement and export |
| Shaswitha | Adjacency-list directed graph | DAG algorithms, readiness metadata and trace validation |
| Divyaa | Chained hash table | Task-ID lookup, parser and metrics |

Use `std::vector`/arrays as storage, but implement heap sifting, ring operations,
adjacency links, hashing and rehashing yourselves. Do not replace the assigned
structures with equivalent STL containers. Test-only reference containers are
allowed. These are team design decisions, not a claim about additional course rules.

## Directory structure

```text
run.sh                    argument-free build, tests and checkpoint outputs
code/include/gpu/         shared types, module contracts and ordering keys
code/include/gpu/ds/      four custom data structure APIs
code/src/                 current executable; implementation modules go here
code/tests/               tests compiled into the executable
code/data/fixtures/       valid and invalid workload inputs
code/data/expected/       hand-calculated test oracles, not measured output
code/config/              planned default experiment configuration
code/build/               generated executable, ignored by Git
code/results/             generated output, ignored by Git
docs/                     contracts, tasks and contribution records
.github/                  CI and pull-request template
```

Start with [the team quickstart](docs/QUICKSTART.md),
[shared contracts](docs/CONTRACTS.md), and [the task board](docs/TASKS.md).
Record real implementation and review evidence in [contributions](docs/CONTRIBUTIONS.md).
For Pranav's engine walkthrough and teammate handoffs, read
[the event-engine guide](docs/EVENT_ENGINE.md).

## Milestones

- November 1, 2026: agree on the contracts in this checkpoint.
- November 4: four custom structures with meaningful unit tests.
- November 8: dependent FIFO workload running through all four structures.
- November 12: central FIFO/SJF/CP and independent trace verification.
- November 20: required code and documentation merged and tested.
- November 21-23: final integration; November 24-30: fixes, report and rehearsal.

The final package is `Group4_Project.zip` with a report, dedicated code folder,
root README and executable `run.sh`. This checkpoint alone is not submission-ready.
