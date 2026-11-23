# Final integration - November 23, 2026

All four custom data structures are now used in the production CSV-to-results
pipeline. The final branch preserves both parent histories when integrating
Divyaa's `feature/hash-table-parser-metrics` with the tested graph/scheduler/engine
branch. One shared test-runner conflict was resolved by keeping every suite.

## Completed behavior

`run.sh` compiles from source, runs all tests and executes the default experiment
matrix without arguments or downloads. The driver loads 48 saved CSV inputs,
parses IDs through the custom chained hash table, validates the adjacency-list
DAG, precomputes critical ranks, constructs a fresh production scheduler, runs
the heap-based event engine, independently verifies the trace, computes metrics
and exports the result. There are 756 benchmark runs and five six-task examples.

The default workload plan is in `code/config/README.md`. Preprocessing counters
are recorded once per input; runtime counters reset per configuration. Graph
vertex/edge counts are structural sizes, while successor_visits is measured
in the event engine. Neither is a wall-clock timing measurement.

## Integration fixes and tests

- Defined the previously declared `load_and_validate` bridge.
- Preserved all 16 hash/parser/metrics groups alongside existing module tests.
- Added seven full-pipeline groups: hand oracle through file input and export;
  forward references with multiple predecessor completions and competing release
  times; empty input; malformed IDs/cycles/rank overflow; hash instrumentation;
  all saved generator inputs and experiment count; negative-ready metric guard.
- Added the actual production `simulate()` path to the existing 960 randomized
  tick-oracle comparisons (including releases and permuted dense IDs).
- Corrected Linux sanitizer execution to run from `code/`, matching fixture paths.
- Guarded negative metric timestamps before subtraction and used overflow-safe
  nearest-rank p95 integer arithmetic.
- Kept the previously tested busy-chart filename fix with compute-unit counts.

## Evidence

- Local full run: **184/184 module/full-pipeline groups and 18/18 engine groups**.
- Every one of the **756 benchmark runs** independently trace-verified.
- Separate Python output audit: metric formulas, dimensions, exact chain and
  central fork-join makespans, static owner-skew makespans, event counts, graph
  traversal counts, six-task traces and SVG XML all pass.
- GitHub Linux/macOS and Linux sanitizer results are linked from the final PR.
- Clean ZIP extraction and byte-comparison evidence is recorded in the delivered
  `SUBMISSION_CHECK.txt` after the final package is tested.

The final report and presentation guide describe this implementation. Older
`INTEGRATION_CHECK.md`, `AAYUSH_WORK_LOG.md`, engine checkpoint output examples
and the earlier planning PDFs are historical records.
