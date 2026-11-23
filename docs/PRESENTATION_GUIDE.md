# Eight-minute team walkthrough

This is a suggested rehearsal, not evidence that anybody has presented or
independently reviewed the code. Every member should run the project once.

## 0:00-1:30 - Divyaa: IDs, hashing, parsing and metrics

Open `code/src/id_hash_table.cpp` and `parser.cpp`. Show bucket heads, the entry
pool, FNV-1a, collision traversal and doubling rehashes at load > 0.75. Explain
why two passes allow forward references and why duplicates must be rejected.
Show `load_and_validate` and the CSV fixture. Metrics: wait = start-ready,
turnaround = finish-release, utilization = sum(duration)/(P*makespan). Explain
nearest-rank p95 and population busy-time CV. Distinguish expected constant-time
hash operations (with bounded key lengths) from worst-case linear chains.

## 1:30-3:00 - Shaswitha: dependency graph and verification

Open `directed_graph.cpp`, `dag_algorithms.cpp`, `result_validation.cpp`. Draw
A,B -> D; C -> E; D,E -> F. Explain adjacency head/next links, immutable initial
indegrees, Kahn cycle rejection, and reverse-topological longest-tail ranks.
Ready time is max(release, all predecessor finishes). The engine copies indegrees
so repeated runs reuse the same graph. The independent verifier rejects dependency
violations, overlapping unit intervals, missing tasks and inconsistent totals.
It checks feasibility, not the optimality of a scheduling choice.

## 3:00-5:00 - Pranav: heap and event engine

Open `binary_heap.hpp` and `simulator.cpp`. Walk through sift-up and sift-down,
then arrivals/completions at one timestamp. Completions are processed before
arrivals; newly ready tasks are sorted by dense ID before dispatch. Advance the
clock directly to the next event. Show event pushes/pops = 2N and successor visits
= E in generated counters. Explain why unit scanning contributes NP, so the full
runtime is not just N log N. Describe the test-only tick oracle and production
cross-check. Run `./run.sh` beforehand; demonstrate generated output live.

## 5:00-7:00 - Aayush: deque, policy and load balancing

Open `ring_deque.hpp`, `policies.cpp`, `scheduler.hpp`. Explain head/count,
wraparound, doubling and amortized constant-time pushes. Central FIFO shares one
queue; SJF/CP reuse the custom heap. Static ownership is TaskId % P. Stealing first
gives all idle owners a turn, then takes from the back of cyclic victim queues.
Show the six-task chart: FIFO/SJF/static/stealing 13, CP 11 ticks. Then use
owner_skew_N10000_P8 to show distribution effects. Always compare skew strategies
at the same P because that workload's durations depend on P.

## 7:00-8:00 - Results, limitations and reproducibility

Open `code/results/index.html`. Explain that chain speedup is limited by its DAG,
that SJF minimizes neither all makespans nor all waiting metrics universally, and
that CP uses offline duration/rank information. Our units are virtual and identical;
we do not model CUDA, transfer/memory cost or dispatch overhead. Show the report's
measured findings and `run_manifest.txt`. A clean ZIP needs only a C++17 compiler
and a POSIX shell. The grading command is exactly `./run.sh`.

## Practice questions for everyone

1. Why is release time not the same as ready time?
2. What prevents a task from being enqueued or executed twice?
3. Why copy indegrees rather than decrement the original graph?
4. What happens if the graph has a cycle or a CSV ID is unknown?
5. Why can static assignment leave a unit idle while another queue is nonempty?
6. Why does a resize cost O(N) but a sequence of deque pushes average O(1)?
7. What distinguishes virtual ticks, operation counts and host seconds?
8. Which tests have hand-calculated answers independent of the implementation?
9. What is each student's actual contribution, and where was AI assistance used?
10. What can the verifier guarantee, and what remains a policy test?
