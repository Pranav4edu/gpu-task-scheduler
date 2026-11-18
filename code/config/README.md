# Experiment configuration

The default experiment driver will be added after the simulation baseline.
This checkpoint has no active benchmark configuration and does not claim final results.

Planned: central FIFO/SJF/CP and static FIFO, P=1/2/4/8, saved workload CSVs,
trace validation before metrics, and optional work stealing after the core passes.
Keep the public entry point argument-free: `./run.sh`.

Owner-skew workloads (12 saved CSVs plus `owner_skew_settings.txt`) are in
`../data/workloads/`; see that settings file for the generator rules.
