# Deterministic experiment configuration

`default_experiment_inputs()` in `src/experiments.cpp` defines the fixed no-argument
suite. It reads the 48 saved CSV files; it does not generate or download inputs at
run time. `results/experiment_manifest.csv` records the actual run matrix.

Sizes: 100, 1,000 and 10,000 tasks. Unit counts: 1, 2, 4 and 8.
All five supported configurations run on each of 36 main inputs. For 12 owner-skew
inputs use only central FIFO, static FIFO and work-stealing FIFO at the input's P.
There are 756 benchmark rows and five separate six-task example rows.

- Chain: N tasks, duration 3, release 0, edges i -> i+1.
- Fork-join: root + N-2 branches + sink, duration 3, release 0.
- Mixed: independent tasks; durations 1..100; release = ((i/25) mod 4)*20.
- Layered: width 10; same duration/release rules; up to three distinct predecessors
  sampled from the previous layer (duplicate selections are discarded).
- Mixed/layered seeds: 101, 202, 303, 404 and 505. Generator state is unsigned
  32-bit `state = state*1664525 + 1013904223`; durations use `1 + state % 100`,
  predecessor selection uses `state % 10`. This is a reproducible bounded test
  generator, not a statistical model of real GPU workloads. No standard-library
  random distribution mapping is needed; saved CSVs remain authoritative.
- Owner skew: see `data/workloads/owner_skew_settings.txt`. Heavy tasks are those
  assigned to unit 0 by TaskId % P, duration 10 versus 1 for other tasks.

After a successful build, a developer may regenerate the saved main inputs with:

```sh
cd code
./build/gpu_sim --write-experiment-inputs
./build/gpu_sim --write-skew-workloads
```

These developer commands are not part of grading. Tests compare every saved main
input byte-for-byte with its generator and separately check owner-skew fixtures.
Preprocessing happens once per input and simulation counters reset each run.
No host-runtime speedup is reported; compiler/host metadata describes reproducibility.
