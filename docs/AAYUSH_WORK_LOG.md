# Aayush's work log (RingDeque, scheduler, placement, export)

Honest record, updated after every step. "AI-assisted" means drafted with Claude
Code at Aayush's request. Aayush's own review and explanation evidence is added
only when it actually happens; the checklist items below start unchecked.

## Step 1 - RingDeque

**Files:** `code/include/gpu/ds/ring_deque.hpp`, `code/tests/test_ring_deque.cpp`,
registration in `code/include/gpu/self_test.hpp` and `code/tests/self_test.cpp`.

**Status:** implemented (AI-assisted); 8 unit tests pass; owner review pending.

**Representation.** Fixed array of `capacity` slots, plus `head_` and `count_`.
Logical element k is at `(head_ + k) % capacity`. `count_` (not a tail index)
distinguishes empty from full.

**Invariants.** capacity >= 1; count <= capacity; head < capacity.
Checked by `invariant_holds()`.

**Complexity.**

| Operation | Cost | Assumption |
| --- | --- | --- |
| front, back, at, size, empty | O(1) | |
| pop_front, pop_back | O(1) | no shifting; no shrinking |
| push_front, push_back | amortized O(1) | doubling growth |
| push that triggers resize | O(Q) | Q elements copied in logical order |
| storage | O(peak Q + 1) | never shrinks, so not O(current Q) |

**Counters** (`stats()`): pushes, pops, resizes, elements_copied, peak_size.
Failed pops (empty) are not counted. Counts are per deque lifetime.

**Design points to be able to explain.**
- `push_front` computes `(head + capacity - 1) % capacity`; the `+ capacity`
  prevents unsigned underflow.
- `grow()` copies in logical order and resets `head_` to 0, which also unwraps.
- Each push copies its argument first, so `push_back(d.front())` is safe when the
  resize reallocates storage.
- Capacity overflow is checked before doubling (`std::length_error`).
- `std::deque` appears only in tests as an independent reference model.

**Tests** (`code/tests/test_ring_deque.cpp`): capacity 0/1; wraparound at both
ends; growth while wrapped; aliased value across resize; empty access throws
(and failed pops are not counted); drain/refill cycles; 20,000 random mixed
operations versus `std::deque`; geometric growth (1,000 pushes -> 10 resizes,
1,023 element copies).

**How to run.** From the repository root: `sh run.sh`, then look for the eight
`ring deque ...` lines marked PASS and `19/19 foundation tests passed` in the
output or in `code/results/test_summary.txt`.
(A segfault at the very end comes from `--foundation-demo` in `main.cpp`, which is
Pranav's heap demo and does not use the deque. It also happens on the unmodified
starter. To be reported to Pranav.)

**Owner review checklist (check only when actually done):**
- [ ] Read `ring_deque.hpp` line by line.
- [ ] Mutation check: break one line (e.g. remove `+ capacity()` in `push_front`)
      and confirm a test fails, then restore it.
- [ ] Can explain wraparound, resize and amortized cost without notes.
- [ ] Reviewed by another team member (add PR/commit link).

## Step 2 - Central FIFO scheduler

**Files:** `code/include/gpu/scheduler.hpp`, `code/src/policies.cpp`,
`code/tests/test_scheduler.cpp`; registered in `self_test.hpp` / `self_test.cpp`.

**Status:** implemented (AI-assisted); 8 unit tests pass (27/27 total); owner
review pending. SJF/CP and static queues are later steps.

**What it is.** `CentralFifoScheduler` implements the shared `Scheduler`
interface from `contracts.hpp` with one global `RingDeque<TaskId>`.
- `enqueue_ready(batch)`: appends the engine's batch (already in ascending TaskId
  order) to the back, so tasks that were already waiting stay ahead.
- `dispatch(idle_units)`: pops from the front, one task per idle unit in the given
  (ascending) order; returns `min(idle, queued)` assignments with `stolen = false`.
- `idle_units` must be strictly ascending. A bad list throws `invalid_argument`
  before anything is popped, so a failed call leaves the queue unchanged.
- The scheduler does not touch the clock, task state or dependency counters.

**Complexity.** enqueue: O(B) for a batch of B (amortized O(1) per task).
dispatch: O(U) for U idle units. Storage: O(peak queue length).

**Tests** (`test_scheduler.cpp`, expected values worked out by hand): empty queue
/ no idle units; fewer idle than queued; more idle than queued; old entries stay
ahead of a new batch; every task dispatched exactly once over many epochs; bad
idle lists rejected with no side effects; works through the `Scheduler*` base
interface; deque counters (pushes, pops, peak size).

**How to run.** `cd gpu-task-scheduler && sh run.sh` (Git Bash/WSL). Look for the
eight `central FIFO ...` PASS lines and `27/27 foundation tests passed`.

**Owner review checklist (check only when actually done):**
- [ ] Read `scheduler.hpp` and `policies.cpp`.
- [ ] Mutation check: change `push_back` to `push_front` in `enqueue_ready` and
      confirm tests fail; restore it.
- [ ] Can explain why the validation happens before any pop.

## Step 3 - SJF and critical-path schedulers + six-task oracle

**Files:** `code/include/gpu/scheduler.hpp` (added `CentralHeapScheduler<Before>`,
`CentralSjfScheduler`, `CentralCpScheduler`), `code/tests/test_policies.cpp`;
registered in `self_test.hpp` / `self_test.cpp`.

**Status:** implemented (AI-assisted); 10 new tests pass (37/37 total); owner
review pending.

**What it is.** One class template over the comparator, using Pranav's shared
`BinaryHeap<ReadyKey, Before>` (no second heap).
- SJF: smallest duration, then earliest ready time, then lowest TaskId.
- CP: largest critical_rank, then earliest ready time, then lowest TaskId. Ranks are
  compared directly, never negated (a rank near the 64-bit maximum is tested).
- Entries are immutable `ReadyKey`s, so heap order cannot be invalidated while a
  task waits. Already-waiting tasks compete with each new batch (unlike FIFO), and
  batch/insertion order cannot change the result (tested over all 120 orderings).
- `dispatch` has the same contract as FIFO: strictly ascending idle units,
  validated before any pop, one task per idle unit, `stolen = false`.

**Complexity.** enqueue O(B log H) for a batch of B with H queued; dispatch
O(U log H) for U idle units; storage O(peak H). Heap counters (pushes, pops,
comparisons, swaps, peak) are exposed via `queue_stats()`.

**Oracle test.** `test_policies.cpp` contains a TEST-ONLY mini simulator (array
scans, not the project engine) that replays the hand-calculated six-task example
(P=2, releases 0) through the scheduler classes and compares the full trace:
- FIFO and SJF: A[0,2) C[2,8) E[8,12) F[12,13) on unit 0; B[0,3) D[3,5) on unit 1;
  makespan 13.
- CP: C[0,6) E[6,10) F[10,11) on unit 0; B[0,3) A[3,5) D[5,7) on unit 1; makespan 11.
- P=1: makespan 18 (total work) for FIFO and CP.
The expected values come from `docs/CONTRACTS.md`, not from running this code.
It can be removed or replaced by the real engine test once the engine exists.

**Mutation check done during development.** Changing FIFO `push_back` to
`push_front` in a throwaway copy made 4 tests fail (including the FIFO oracle).

**How to run.** `cd gpu-task-scheduler && sh run.sh` (Git Bash/WSL). Look for the
ten PASS lines for SJF / CP / priority / `six-task oracle` and
`37/37 foundation tests passed`.

**Owner review checklist (check only when actually done):**
- [ ] Read `CentralHeapScheduler` in `scheduler.hpp` and `test_policies.cpp`.
- [ ] Mutation check: in `priority.hpp` change `a.critical_rank > b.critical_rank`
      to `<` and confirm the CP tests fail; restore it.
- [ ] Re-derive one oracle trace (e.g. CP) by hand on paper.
- [ ] Can explain why heap ties must be fully ordered (ready time, then TaskId).

## Step 4 - Static round-robin placement and scheduler factory

**Files:** `code/include/gpu/scheduler.hpp` (added `StaticFifoScheduler`,
`make_scheduler`), `code/src/policies.cpp`, `code/tests/test_static_scheduler.cpp`
(new), `code/tests/test_policies.cpp` (two new oracles; mini simulator generalized
to `run_tasks`); registered in `self_test.hpp` / `self_test.cpp`.

**Status:** implemented (AI-assisted); 12 new tests pass (49/49 total); owner
review pending.

**What it is.** `StaticFifoScheduler(P)` keeps one `RingDeque<TaskId>` per unit.
A task becomes eligible -> goes to the back of queue `TaskId % P`. A unit can only
take the front of its own queue; an idle unit whose queue is empty gets nothing
even if other queues are long. That isolates idle capacity caused by placement.
- `owner_of(task, P)` is the single definition of ownership (matches
  `TaskRecord::initial_owner`); P = 0 is rejected.
- `dispatch` validates first: idle units strictly ascending and each < P, else
  `invalid_argument` before any pop.
- Only eligible tasks are ever enqueued (the engine enqueues them), so blocked
  tasks never occupy a local queue.
- `make_scheduler(RunConfig)` runs `validate_config`, then returns central
  FIFO / SJF / CP or static FIFO. Work stealing throws `std::logic_error` until
  and unless it is implemented.

**Complexity.** enqueue O(B); dispatch O(U) for U idle units; queued() O(P);
storage O(peak total queued + P).

**Oracles (hand-calculated; test-only mini simulator, not the engine).**
- Six tasks, P=2, static: A,C,E on unit 0 and B,D,F on unit 1:
  A[0,2) C[2,8) E[8,12) on u0; B[0,3) D[3,5) F[12,13) on u1; makespan 13 (same as
  central FIFO but F runs on unit 1).
- Skew example, P=2, durations 5,1,5,1: static makespan 10 (unit 1 sits idle after
  t=2) versus central FIFO makespan 6. This is the placement effect the project
  compares.

**Mutation check done during development.** Changing `owner_of` to
`(task + 1) % P` in a throwaway copy made 6 tests fail. This also exposed a weakness
in my own tests (indexing an empty result crashed instead of failing); the three
scheduler test files now use a `first_of()` helper that fails cleanly.

**How to run.** `cd gpu-task-scheduler && sh run.sh`. Look for the `static FIFO`,
`scheduler factory` and the two new oracle PASS lines and
`49/49 foundation tests passed`.

**Owner review checklist (check only when actually done):**
- [ ] Read `StaticFifoScheduler` and `make_scheduler`.
- [ ] Mutation check: delete the `if (mine.empty()) continue;` line's protection
      by letting an idle unit pop another queue (this would be stealing) and
      confirm `idle unit cannot take another unit's work` fails; restore it.
- [ ] Re-derive the skew oracle (10 vs 6) by hand.
- [ ] Can explain why static placement can idle units while work is queued.

## Step 5 - Export (CSV + SVG charts)

**Files:** `code/include/gpu/export.hpp`, `code/src/export.cpp`,
`code/tests/test_export.cpp`; registered in `self_test.hpp` / `self_test.cpp`.
Also a small portability fix in `run.sh` (see below).

**Status:** implemented (AI-assisted); 10 new tests pass (59/59 total); owner
review pending.

**What it is.** Pure formatting of values other modules already computed; it never
recomputes a metric. Output is deterministic (same input -> identical bytes).
- `ExperimentRow {workload, compute_units, policy, distribution, Metrics,
  busy_ticks}`: one completed run.
- `results_csv(rows)`: header `workload,compute_units,policy,distribution,config,
  makespan,mean_ready_wait,p95_ready_wait,mean_turnaround,utilization,
  busy_time_cv`. Missing metrics (nullopt) print `N/A`; doubles use 6 decimals;
  fields with comma/quote/newline are quoted per CSV rules.
- `svg_bar_chart(title, y_label, bars)`: generic standalone SVG bar chart. Bar
  height = value / max * 300 px; text is XML-escaped; empty data gives a valid
  "no data" SVG; all-zero data does not divide by zero; negative/NaN/inf throws.
- `makespan_chart_svg(rows, title)`: chart 1, one bar per row ("central_fifo P=2").
- `busy_time_chart_svg(row)`: chart 2, useful busy ticks per unit ("Unit 0", ...).
- `write_text_file(path, content)`: truncating write; parent directory must exist
  (`run.sh` creates `results/`); throws `runtime_error` naming the path.

**Complexity.** O(R) for R rows in CSV; O(B) for B bars in SVG. No algorithmic
data-structure claim here: this is output code.

**Tests** (hand-derived expectations): names/labels; exact CSV text including N/A
and quoting; header-only CSV; makespan chart geometry for values 13 and 11 (bar
heights 300.00 and 253.85 = 11/13*300); busy chart geometry for the six-task static
busy times 12 (A+C+E) and 6 (B+D+F) -> 300.00 and 150.00; XML escaping; empty and
all-zero charts; bad values rejected; determinism; file round trip and missing-
directory error. Metric numbers in CSV tests are formatting inputs, NOT measured
results.

**Mutation check done during development.** Halving the bar height formula in a
throwaway copy made 2 chart tests fail.

**Visual check.** A sample makespan chart was rendered in a browser (bars 13, 11,
13 for central FIFO, central CP, static FIFO on the six-task example).
`code/results/sample_*.svg|csv` are throwaway samples in the ignored results
folder, not project results.

**Environment problem found and fixed (affects the whole team on Windows).**
Programs built with MSYS2 g++ and run from Git Bash loaded Git's older
`libstdc++-6.dll` first. Using `std::filesystem` made the program fail to start
(exit 127), and file streams segfaulted. This was the cause of the segfault in
Pranav's `--foundation-demo` that appeared at the end of `run.sh`. Fixes:
(1) no `std::filesystem` anywhere in my code or tests; (2) `run.sh` now adds
`-static` only on MINGW/MSYS/CYGWIN shells. Linux/macOS builds are unchanged.
`run.sh` belongs to Pranav, so tell him about this change.

**How to run.** `cd gpu-task-scheduler && sh run.sh`. Look for the ten `export ...`
PASS lines, `59/59 foundation tests passed`, and NO segfault line at the end.

**Owner review checklist (check only when actually done):**
- [ ] Read `export.hpp` / `export.cpp`.
- [ ] Mutation check: change `* plot_h` in the bar-height line of `svg_bar_chart`
      to `* plot_h / 2` and confirm the two geometry tests fail; restore it.
- [ ] Open `code/results/sample_makespan.svg` in a browser and check it by eye.
- [ ] Tell Pranav about the `run.sh` Windows static-link change.

## Step 6 - Owner-skew workload fixtures

**Files:** `code/include/gpu/workloads.hpp`, `code/src/workloads.cpp`,
`code/tests/test_workloads.cpp`, `code/tests/oracle_sim.hpp` (the test-only mini
simulator moved out of `test_policies.cpp` so two test files share it), saved data in
`code/data/workloads/` (12 CSVs + `owner_skew_settings.txt`), and a developer flag
`--write-skew-workloads` in `code/src/main.cpp` (Pranav's file; small additive
change, tell him).

**Status:** implemented (AI-assisted); 5 new tests pass (64/64 total); owner
review pending.

**The workload family.** N independent tasks, all released at time 0, no
dependencies, named T0, T1, ... in row order. Task i has duration 10 ("heavy") if
`i % P == 0`, else 1 ("light"). Under static round robin the owner of task i is
`i % P`, so unit 0 receives all the heavy work while the other units finish early
and idle. A central queue lets idle units help. No random numbers, so the files are
identical on every platform. One file per (N in {100, 1000, 10000}) x (P in {1, 2, 4,
8}) = 12 files, matching the "12 owner-skew workloads" in the plan. The pattern
depends on P, so compare central vs static only at the same P (never cross-P
speedup); with P = 1 both are identical.

**Hand-derived expectations (used by the tests).**
- heavy task count = ceil(N/P); total work W = 10*ceil(N/P) + (N - ceil(N/P)).
- Static makespan = 10 * ceil(N/P) (unit 0 runs all heavy tasks serially). E.g.
  N=100, P=4: 250; N=100, P=2: 500.
- Central FIFO must lie within list-scheduling bounds:
  max(ceil(W/P), 10) <= makespan <= floor(W/P) + 10, and for P >= 2 be strictly
  smaller than the static makespan. For P = 1 both equal W.

**Tests.** Exact text of a small example; invalid settings rejected; all 12 generated
files follow the input format and pattern (ids unique, release 0, empty
dependencies, heavy count and total work match the formulas); saved files equal
generator output byte for byte (so a hand-edited fixture is caught); mini-simulator
runs for N = 100 and 1,000 at every P check the makespan facts above and that
static runs every task on its owner. N = 10,000 is covered by the format/saved-file
tests only (the test simulator is O(N^2)).

**Mutation checks done during development.** Corrupting one saved CSV failed the
saved-fixture test; changing the heavy rule to `i % P == 1` failed four tests.

**Important limits.** These files have NOT yet been loaded through Divyaa's parser
or run through Pranav's engine (neither exists yet). Format conformance is checked
against `docs/CONTRACTS.md` by my own tests only. No measured results are claimed.

**How to run.** `cd gpu-task-scheduler && sh run.sh`; look for the five
`owner-skew ...` PASS lines and `64/64 foundation tests passed`. To regenerate the
fixtures (developers only): `cd code && ./build/gpu_sim --write-skew-workloads`.

**Owner review checklist (check only when actually done):**
- [ ] Open `code/data/workloads/owner_skew_N100_P4.csv` and confirm by eye that
      T0, T4, T8 ... are heavy.
- [ ] Re-derive static makespan 250 and the central bounds for N=100, P=4 by hand.
- [ ] Mutation check: edit one duration in a saved CSV and confirm
      `saved fixtures match generator` fails; restore it.
- [ ] Once Divyaa's parser exists, load all 12 files through it.
- [ ] Tell Pranav about the new `--write-skew-workloads` flag.

## Step 7 - Work-stealing scheduler (optional extension)

**Files:** `code/include/gpu/scheduler.hpp` (`StealStats`, `WorkStealingFifoScheduler`),
`code/src/policies.cpp` (shared `validate_idle_units` helper; `make_scheduler` now
builds stealing), `code/tests/test_stealing.cpp`.

**Status:** implemented (AI-assisted); 11 new tests pass; owner review pending.
Built early because it can be tested without the engine. Per the plan it still has to
pass the engine's trace validator before it may be reported as a finished feature;
until then, treat it as "implemented and unit-tested, not engine-validated".

**Rules implemented (from `CONTRACTS.md`).** Same owner = `TaskId % P` and per-unit
`RingDeque` as static FIFO, FIFO only. `dispatch`: (1) OWNER PHASE: every idle unit
with a non-empty own queue takes its front; (2) THIEF PHASE: remaining idle units in
ascending id probe `u+1, u+2, ...` cyclically, at most P-1 probes, and take the BACK
of the first non-empty queue (`stolen = true`). A failed search returns nothing
(no retry at the same time stamp). Running tasks are not in any queue, so they can
never be stolen. Counters: searches, probes, steals.

**Complexity.** dispatch is O(U) for owners plus O(U * P) worst case for thieves (each
thief probes at most P-1 queues); this matches the plan's note that stealing can cost
O(N P^2) overall in the worst case. Pops from both ends are O(1).

**Hand-derived tests.** owners first; steal from the back (u0 [0,2,4] -> u1 steals 4);
owner beats a lower-id thief (only task 1 exists: u1 keeps it); cyclic order with
wraparound (P=4, thief u2 sees u3, u0, u1; probe total 1+2+3 = 6); back-to-front order
(10, 6, 2); probe bound (4 thieves x 3 = 12); atomic validation; random runs dispatch
every task once with `stolen == (owner != unit)`; skew oracle durations 5,1,5,1 on P=2:
stealing makespan 7 (central 6, static 10); on the owner-skew family at N=100/1000 and
every P stealing beats static for P>=2 and respects the lower bound.

**Mutation check.** Stealing from the front instead of the back failed 2 tests.

## Step 8 - Data-structure counters (`ds_metrics.csv`)

**Files:** `code/include/gpu/ds_metrics.hpp`, `code/src/ds_metrics.cpp`,
`code/tests/test_ds_metrics.cpp`; `csv_escape` is now public in `export.hpp`;
`oracle_sim.hpp` gained `owner_skew_tasks`.

**Status:** implemented (AI-assisted); 6 new tests pass; owner review pending.

`collect_ds_metrics(scheduler, workload, config)` reads the counters (one row per
queue) and `ds_metrics_csv(rows)` prints
`workload,config,compute_units,structure,instance,pushes,pops,peak_size,resizes,
elements_copied,comparisons,swaps,searches,victim_probes,steals`; counters that do not
apply print N/A. These are operation counts, not simulated or host time.

**Measured counters on two workload sizes (plan requirement).** Produced by running
the schedulers through the test-only mini simulator on the owner-skew workload, and
checked against values derived by hand from the doubling rule (start capacity 8):
- central FIFO, N=100: pushes 100, pops 100, peak 100, 4 resizes, 120 elements copied;
- central FIFO, N=1000: pushes 1000, pops 1000, peak 1000, 7 resizes, 1016 copied;
- static FIFO P=4, N=100: 25 pushes per unit, 2 resizes, 24 copied;
- static FIFO P=4, N=1000: 250 per unit, 5 resizes, 248 copied;
- SJF/CP heap: pushes = pops = peak = N, comparisons > 0 and plausible.
These come from the mini simulator, NOT the project engine. The final report must
replace them with counters from the real pipeline.

**Mutation checks.** Reporting pushes as resizes failed 2 tests; making the deque grow
x3 instead of x2 failed 6.

## Step 9 - Result bundle and handoff documents

**Files:** `result_files` / `write_result_files` / `ResultFileOptions` in `export.hpp`
and `src/export.cpp` (+2 tests in `test_export.cpp`); `docs/INTEGRATION_AAYUSH.md`
(how the engine and driver call my modules); `docs/AAYUSH_PRESENTATION_NOTES.md`
(study and defense notes, worked ring-deque example, numbers to quote).

`write_result_files(rows, "results/")` writes `results.csv`, one
`makespan_<workload>_P<P>.svg` per (workload, P) and per-unit busy charts for
`owner_skew*` rows; duplicate file names are refused so nothing is silently
overwritten. Mutation check: dropping `_P<P>` from the names failed a test.

Also: the `project_status.txt` text in `main.cpp` was updated so it no longer claims
the deque is pending.

## Coding status of Aayush's side

Everything assigned in the plan is implemented and unit-tested: RingDeque, central
FIFO/SJF/CP, static FIFO, optional stealing, CSV/SVG export, counters, owner-skew
fixtures. 83 unit tests pass on the standalone branch. See Step 10 for the real-engine
integration. NOT done: parsing fixtures with a real parser (none exists), a
policy-aware validator for stealing, measured results from the final pipeline, and the
owner-review checklists above (yours).

**How to run (all steps).** `cd gpu-task-scheduler && sh run.sh`; expect
`83/83 foundation tests passed` and exit code 0.

## Step 10 - Integration with the real engine and graph

**Context.** Pranav's engine (PR #3, `bb73b9d`) and Shaswitha's graph/verifier/generators
(PR #4, `611f365`) were already on remote branches; Pranav's
`codex/graph-engine-integration` merges both. This work was merged onto that branch.

**Merge.** Six conflicts, all in shared files (README, TASKS, CONTRIBUTIONS, `self_test.hpp`,
`self_test.cpp`, `main.cpp`); none in module code. Resolved by keeping both sides:
Pranav's engine declarations and flags plus my test registrations and my
`--write-skew-workloads` flag; per-person sections in CONTRIBUTIONS; updated status text.

**New files.** `code/src/simulate.cpp` (the two-argument `simulate()` from
`contracts.hpp`: `make_scheduler` + `simulate_with_scheduler`) and
`code/tests/test_scheduler_engine.cpp` (6 tests).

**What the integration tests check (production schedulers, real graph, real engine,
Shaswitha's verifier).**
- Six-task oracle, P=2, all five configurations: FIFO/SJF trace and makespan 13, busy
  ticks 13/5; CP makespan 11, busy 11/7; static and stealing makespan 13, busy 12/6
  with F on unit 1. Task-level start/finish/unit equal the hand traces.
- Chain, fork-join and layered workloads x P in {1,2,4,8} x five configurations all pass
  `verify_result`; central runs equal my mini simulator task for task.
- Owner-skew fixtures N = 100, 1000, 10000 at P = 1, 2, 4, 8 (60 runs): busy ticks sum to
  total work; static makespan is exactly 10*ceil(N/P); central FIFO stays within the
  list-scheduling bounds and beats static for P>=2; stealing beats static for P>=2.
- Deque counters from the REAL engine match the hand-derived values (N=100: 4 resizes,
  120 copies; N=1000: 7 resizes, 1016 copies).
- Repeated runs are identical; an invalid policy/distribution pair is rejected.

**Results.** Combined suite: 160 foundation/graph/scheduler tests + 18 engine groups pass,
including the six new ones. Also passes with `-D_GLIBCXX_DEBUG -D_GLIBCXX_ASSERTIONS`.
AddressSanitizer/UBSan could not be run locally (MinGW has no sanitizer runtime and WSL is
not installed); GitHub CI runs them on Linux when a branch is pushed.

**Not done.** `--engine-checkpoint` still uses Pranav's test-only schedulers and fixture
output; switching it to the production schedulers and writing `results.csv`,
`ds_metrics.csv` and charts from real runs is the experiment driver's job (Pranav), using
`INTEGRATION_AAYUSH.md`.

**Owner review checklist (check only when actually done):**
- [ ] Read `test_scheduler_engine.cpp` and re-derive one trace from the six-task table.
- [ ] Mutation check: make `StaticFifoScheduler::owner_of` return `(task + 1) % P` and
      confirm the engine rejects the run ("static assignment does not match the task owner").
- [ ] Confirm CI is green on Linux and macOS after the branch is pushed.
