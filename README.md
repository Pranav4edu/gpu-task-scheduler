# GPU Task Scheduler and Load Balancer

CE 6320, Group 4. A complete C++17 discrete-event simulator for dependent tasks
on identical virtual compute units. No GPU hardware, CUDA, downloads or external
C++ libraries are needed.

## Run the entire project

Requires an installed C++17 compiler (GCC or Clang) and a POSIX shell:

```sh
./run.sh
```

No arguments or manual input are required. The script finds its own directory,
compiles all source and tests, stops if a test fails, runs all experiments, verifies
every schedule, and writes fresh results to `code/results/`. It can be invoked
from outside the project directory, including paths containing spaces.
Use `CXX=clang++ ./run.sh` to select a compiler. On Windows use WSL, or Git Bash
with MSYS2 g++ installed, and `sh run.sh`; native Windows execution is not required.

Open `code/results/index.html` in a browser for links to the result charts.
The report is `Group4_Final_Report.pdf` in the submission package. The source
repository keeps its editable report content under `docs/FINAL_REPORT.md`.

## Implemented modules

| Student | Custom data structure | Production use |
| --- | --- | --- |
| Pranav Sathyabodha Kumsi | Binary heap | Event queue; SJF/critical-path priority queues |
| Aayush Agrawal | Circular-array deque | Central FIFO, static local FIFO, work stealing |
| Shaswitha Fernandez | Adjacency-list directed graph | Dependencies, Kahn ordering, critical-path ranks, verification |
| Divyaa Natarajan | Separate-chaining hash table | Task-ID lookup and duplicate dependency detection in the CSV parser |

Each ordinary FIFO experiment uses all four structures. Production code uses
vectors as backing storage; equivalent STL containers are used only in reference
tests. Final integration, additional tests, instrumentation, reports and packaging
were AI-assisted at Pranav's request. See `docs/CONTRIBUTIONS.md` for provenance.

## Experiments

- 36 main inputs: chain, fork-join, mixed independent and layered DAGs, with
  100/1,000/10,000 tasks. Mixed/layered use seeds 101/202/303/404/505.
- Each main input runs on 1/2/4/8 virtual units with central FIFO, SJF and CP,
  static FIFO and work-stealing FIFO: 720 runs.
- 12 owner-skew inputs run central/static/stealing FIFO at their matching P:
  36 runs. These P-dependent inputs are not used for cross-P speedup claims.
- Total: **756 verified benchmark runs**, plus five six-task demonstrations.

Inputs are saved under `code/data/workloads/`; generator rules and regeneration
commands are in `code/config/README.md`. Every measured result is passed through
Shaswitha's independent trace verifier before Divyaa's metrics are exported.

## Generated outputs

| File in `code/results/` | Meaning |
| --- | --- |
| `test_summary.txt` | Individual test groups and pass/fail totals |
| `metrics.csv`, `results.csv` | Identical 756-row virtual metric tables |
| `per_unit.csv` | Useful busy ticks for each unit and run |
| `trace.csv`, `six_task_metrics.csv` | Five production traces and metrics for the hand oracle |
| `ds_metrics.csv` | Event/ready heap and deque counts, work-stealing search counts |
| `engine_metrics.csv` | Timestamp batches, successor visits, dispatch calls, ready tasks |
| `preprocessing.csv` | Hash operations and graph sizes, once per loaded workload |
| `experiment_manifest.csv` | Input path, family, seed, N, E, P and policy for every run |
| `run_manifest.txt` | Compiler, flags, host, source version and input CRC checksums |
| `summary.txt`, `index.html`, `*.svg` | Experiment summary and standalone chart index/files |

Times are virtual ticks, not seconds or GPU measurements. Utilization includes
release-related idle time because makespan is measured from tick zero. Missing
ratios/empty averages are `N/A`. No claim is made that one policy is always best.
Results overwrite the same named files on rerun. Use a fresh directory for a
submission check so stale outputs from previous versions cannot be mistaken for
current results.

## Directory structure

```text
run.sh                         no-argument build + tests + experiments
README.md                      setup and output guide
code/include/gpu/              types, contracts, data structures and module APIs
code/src/                      implementation and experiment driver
code/tests/                    module, reference and integration tests
code/data/fixtures/             small valid/invalid test CSVs
code/data/expected/             hand-calculated test answers
code/data/workloads/            48 saved benchmark inputs and generator settings
code/config/                   deterministic experiment settings
code/build/                    generated binaries (excluded from submission)
code/results/                  regenerated metrics, traces and charts
code/tools/                    optional development tools in the submission
docs/                         design, contribution and presentation notes
.github/workflows/build.yml    Linux/macOS build and Linux sanitizer CI
tools/                        optional developer audits and report/package builders
```

## Additional verification

The grading path requires no Python. Developers with Python 3 can additionally
run `python3 tools/verify_outputs.py` after `./run.sh` to audit the output table,
closed-form workload results, counters and SVG XML independently. The submission
copy places that optional tool under `code/tools/` (pass the project root as its
argument). Linux CI also runs AddressSanitizer and UndefinedBehaviorSanitizer.

See `docs/FINAL_INTEGRATION.md` for final validation evidence, `docs/CONTRACTS.md`
for semantics, and `docs/PRESENTATION_GUIDE.md` for each member's walkthrough.
