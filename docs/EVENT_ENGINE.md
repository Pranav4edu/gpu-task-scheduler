# Pranav's event engine: walkthrough and handoff

The engine is implemented and tested independently as of November 4, 2026.
It uses the custom binary heap already merged in the foundation. The normal
grading pipeline still needs the other three custom structures and production
scheduler. The code is AI-assisted; review it and be able to explain it before
claiming your contribution is complete.

## Your files

- `code/include/gpu/simulator.hpp`: read-only graph adapter, statistics and API.
- `code/src/simulator.cpp`: event loop and assignment validation.
- `code/tests/test_simulator.cpp`: fixtures, reference scheduler, tick oracle,
  engine tests and a labeled demonstration trace.
- `run.sh`: compiles all sources, runs tests and generates checkpoint output.

`simulate_events` returns the existing `RunResult`; shared task/result types and
the other students' DS headers are unchanged. The planned two-argument `simulate`
wrapper remains declaration-only until the production scheduler can be constructed.

## How one run works

1. Check durations, releases, configuration and metadata dimensions. Copy graph
   indegrees into fresh runtime records, and push one arrival per task.
2. Pop every event at the earliest timestamp. Do not advance one tick at a time
   and do not sleep; these are virtual timestamps.
3. Complete all finishing tasks, free their units and decrement each successor's
   remaining predecessor count. Then process all arrivals at that timestamp.
4. Collect only affected tasks, deduplicate them, and mark a task ready when it
   has arrived and its predecessor count is zero. Sort the new batch by task ID.
5. Send the whole batch to the scheduler. Give it ascending idle-unit IDs.
6. Validate the whole assignment batch before starting any returned task. Check
   IDs, eligibility, task/unit uniqueness, owner rules, finish-time overflow and
   whether eligible work was left idle.
7. Record starts/finishes and schedule one completion per assigned task. Repeat
   until every task completes. With unfinished work and no future event, fail
   instead of looping forever.

All runtime state belongs to the engine. The scheduler only owns ready-queue
membership and selection; the graph is immutable. Busy ticks include useful
task duration, not gaps or time waiting for dependencies. A timestamp with several
completions and arrivals produces one readiness batch before dispatch.

## The six-task FIFO trace

Run `./run.sh`. Open `code/results/engine_fixture_trace.csv` and
`code/results/engine_checkpoint.txt`. Both are regenerated, ignored by Git and
explicitly labeled as an isolated engine fixture, not the complete CSV pipeline.

| Time | What the engine does |
| --- | --- |
| 0 | All tasks arrive. A, B and C are ready. Start A on unit 0 and B on unit 1; C waits. |
| 2 | A completes. D still waits for B. Start C on unit 0. |
| 3 | B completes. D becomes ready and starts on unit 1. |
| 5 | D completes. F still waits for E. Unit 1 becomes idle. |
| 8 | C completes. E becomes ready and starts on unit 0. |
| 12 | E completes. F becomes ready and starts on unit 0. |
| 13 | F completes. Makespan is 13; useful busy ticks are 13 and 5. |

There are six arrival pushes and six completion pushes; all 12 are popped. Five
dependency edges are traversed once each. All six tasks become ready exactly once.
CP's independent hand-trace test finishes at tick 11 with busy ticks 11 and 7.
These tests exercise the engine with reference schedulers; they do not establish
that Aayush's production FIFO/SJF/CP implementations are finished.

## What we need from teammates

Nothing blocks this isolated engine checkpoint. The next integration needs:

| Owner | Required handoff | What can happen before then |
| --- | --- | --- |
| Shaswitha | `DirectedGraph::indegrees()` and synchronous `for_each_successor`, validated DAG and CP ranks. The templated traversal definition must be visible in the header. | Run engine tests against `FixtureGraph`; no substitute production graph is supplied. |
| Aayush | A concrete `Scheduler`, configured per run, with `enqueue_ready` and `dispatch`, using the custom deque/heap as planned. | Use test-only reference or scripted assignments. |
| Divyaa | Parsed task records/edges; later metrics from `RunResult`. | Use in-memory tasks and already hand-calculated expected traces. |

The existing graph/scheduler signatures remain intact. Ask their owners to review
the additive adapter, work-conservation rule and `initial_owner` meaning in
CONTRACTS.md when they integrate. No message has been sent on their behalf.

Once Shaswitha's graph and Aayush's scheduler are implemented, the adapter body
looks like this (integration example, not currently compiled production code):

```cpp
RunResult run_with_scheduler(const ValidatedWorkload& workload,
                            const RunConfig& config, Scheduler& scheduler) {
    DependencyView dependencies{
        workload.graph.indegrees(),
        [&workload](TaskId id, const SuccessorVisitor& visit) {
            workload.graph.for_each_successor(id, visit);
        }
    };
    return simulate_events(workload.tasks, dependencies,
                           workload.critical_ranks, config, scheduler);
}
```

The final `simulate(workload, config)` wrapper will construct the appropriate
fresh scheduler and call this engine. Preserve the test adapters in tests only;
use all four custom structures in the real default pipeline.

## Verification and limits

The standard run includes 11 heap/foundation tests and 14 engine test groups.
Engine coverage includes hand traces, simultaneous arrivals/completions, joins,
late releases, FIFO waiting order, idle units, repeated runs, Tick overflow,
invalid assignments, static ownership, metadata errors and a scripted steal.
The scripted steal tests only engine accounting, not a work-stealing algorithm.

One group runs 960 comparisons: 80 deterministic random DAGs, three unit counts
and four configurations (central FIFO/SJF/CP and static FIFO). IDs are permuted
away from topological order and adjacency insertion order is reversed. The
independent oracle advances one tick at a time and scans predecessor records;
it does not reuse the heap, readiness counters or production comparators.

Graph validity and CP-rank computation are preprocessing responsibilities. The
engine catches many malformed metadata cases but does not replace Shaswitha's
graph validation or independent final trace validator. It also does not verify
policy choice or the owner-first stealing algorithm inside a scheduler.

## Complexity you should explain

Let N be tasks, E edges, P units and B processed timestamps (B <= 2N).
For a valid workload with O(outdegree) successor traversal, event-heap work is
O(N log(N+1)), successor work O(E), and all readiness-batch sorts total at most
O(N log(N+1)). Unit discovery costs O(BP). Returned assignment validation sorts
by unit and task, totaling O(N log(min(N,P)+1)). Add the scheduler's own costs;
they are not hidden in the engine bound.

Engine-owned memory is O(N+P), excluding immutable graph/input and scheduler
storage. There is one arrival and one completion per task, at most N outstanding
events, and each graph edge is visited once at its source's completion. The
non-owning callback adapter adds constant dispatch overhead per visit, not a copy
of the graph. Counter output measures operations, not hardware GPU performance.

## Your next review

Trace the six-task example by hand, then explain why dispatch waits until the
whole timestamp is processed. Locate the predecessor-count copy, deduplication
flags, checked finish addition and duplicate-assignment checks. Run the tests,
review the draft PR, and have Aayush review the engine boundary. Next, replace
test-only dependencies at the production call site when the actual modules land.
