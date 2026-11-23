# Integration guide: scheduler, export and workloads (Aayush's modules)

For whoever writes the experiment driver and `main.cpp` (the engine is Pranav's
`simulate_events` / `simulate_with_scheduler` in `gpu/simulator.hpp`). Everything here is
implemented, unit-tested, and now also exercised through the REAL engine, graph and
verifier by `code/tests/test_scheduler_engine.cpp`. The engine's own checks (assignment
validity, static ownership, steal flags, no eligible work left idle) all pass with these
schedulers.

## 1. Scheduler (what the engine calls)

```cpp
#include "gpu/scheduler.hpp"

RunConfig config{P, Policy::critical_path, Distribution::central};
auto scheduler = make_scheduler(config);        // fresh object for EVERY run
```

`make_scheduler` validates the configuration and returns one of: central FIFO / SJF /
CP, static FIFO, or stealing FIFO. SJF/CP with a local distribution, `P == 0`, and
unknown enum values throw `std::invalid_argument`.

Inside the event loop, at each timestamp, after completions and arrivals have been
processed:

```cpp
std::vector<ReadyKey> batch;   // tasks that became eligible at this timestamp,
                               // sorted by ascending TaskId, each task exactly once
// ReadyKey{task, duration, critical_rank, ready_time}; ready_time = max(release, last predecessor finish)
scheduler->enqueue_ready(batch);

std::vector<UnitId> idle;      // ALL currently idle units, STRICTLY ascending
for (const Assignment& a : scheduler->dispatch(idle)) {
    // start a.task on a.unit now; push exactly one completion event
    // a.stolen is true only for stealing runs when a.unit != owner of a.task
}
```

Rules the scheduler relies on (it does not check the ones marked *):

| Rule | Enforced? |
| --- | --- |
| `idle` strictly ascending, no duplicates, every id `< P` | yes: `std::invalid_argument` before anything is removed |
| each task enqueued at most once* | no, engine guarantees it |
| batch sorted by ascending TaskId* | no (FIFO order depends on it; SJF/CP do not) |
| engine must START every returned assignment* | the task has already left the queue; dropping one loses the task |
| at most one assignment per idle unit | yes |
| scheduler never changes clock, task state or dependency counters | by design |

Behaviour summary:

| Config | Ready queue | Chooses next task |
| --- | --- | --- |
| central FIFO | one `RingDeque<TaskId>` | oldest queued; waiting tasks stay ahead of a new batch |
| central SJF | one `BinaryHeap<ReadyKey, SjfBefore>` | smallest duration, then ready_time, then TaskId |
| central CP | one `BinaryHeap<ReadyKey, CriticalPathBefore>` | largest critical_rank, then ready_time, then TaskId |
| static FIFO | `RingDeque` per unit, owner = `TaskId % P` | a unit runs only its own queue's front |
| stealing FIFO | same queues | owners first; an unlucky idle unit steals the back of the first non-empty queue after it (cyclic, at most P-1 probes) |

`TaskRecord::initial_owner` is filled in by the engine: `TaskId % P` for static and
stealing runs, and the unit that ran the task for central runs. Schedulers do not set it.

Shortcut: `RunResult simulate(const ValidatedWorkload&, const RunConfig&)` (in
`code/src/simulate.cpp`) is `make_scheduler(config)` followed by
`simulate_with_scheduler`. To also read the scheduler's counters afterwards, call those
two yourself and keep the scheduler object:

```cpp
auto scheduler = make_scheduler(config);
RunResult result = simulate_with_scheduler(workload, config, *scheduler, &engine_stats);
verify_result(workload, config, result);
auto rows = collect_ds_metrics(*scheduler, workload_name, config);
```

## 2. Data-structure counters (`ds_metrics.csv`)

After a run finishes, before the scheduler is destroyed:

```cpp
#include "gpu/ds_metrics.hpp"
auto rows = collect_ds_metrics(*scheduler, workload_name, config);   // append to a vector
write_text_file("results/ds_metrics.csv", ds_metrics_csv(all_rows));
```

Counters are cumulative over the scheduler's lifetime, hence one scheduler per run.
These are operation counts, not simulated time and not host runtime.

## 3. Results and charts

```cpp
#include "gpu/export.hpp"
ExperimentRow row;           // workload, compute_units, policy, distribution,
row.metrics = compute_metrics(workload, result);       // Divyaa's Metrics
row.busy_ticks = result.busy_ticks;                    // from RunResult
// ...collect all rows, then once at the end:
write_result_files(rows, "results/");  // directory must already exist (run.sh: mkdir -p results)
```

Writes `results.csv`, one `makespan_<workload>_P<P>.svg` per (workload, P) group and
per-unit `busy_<workload>_<config>_P<P>.svg` charts for `owner_skew*` rows. The unit
count keeps charts distinct when the same workload and policy run at different P.
`result_files(rows)` returns the same
files in memory. No `std::filesystem` is used on purpose (see the Windows note in
`AAYUSH_WORK_LOG.md`, Step 5).

## 4. Workloads

`code/data/workloads/owner_skew_N{100,1000,10000}_P{1,2,4,8}.csv` (12 files) and
`owner_skew_settings.txt`. Input format per `CONTRACTS.md`. Compare central and static
only at the same P. Regenerate with `./build/gpu_sim --write-skew-workloads` from
`code/` (tests fail if the saved files differ from the generator).

## 5. Known limits

- `RingDeque<T>` needs a default-constructible, copyable `T` (it pre-sizes its array).
- Schedulers are single-threaded and not copyable-safe across runs; build a new one per run.
- The test-only mini simulator (`tests/oracle_sim.hpp`) is O(N^2); do not use it for N = 10,000.
- No benchmark results beyond the unit/integration tests exist yet. Every expected number
  in the tests is hand-derived; the real-engine counter test confirms the same values.
- The independent verifier does not check scheduling-policy order, ownership or steal
  flags. The engine checks ownership and steal flags itself; policy order is covered by
  the oracle tests.
