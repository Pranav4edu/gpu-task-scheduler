#pragma once
#include "gpu/contracts.hpp"
#include <string>
#include <utility>
#include <vector>

namespace gpu {

// Result export (Aayush's module): CSV rows and two simple SVG charts.
// Everything here only FORMATS values that other modules already computed. It never
// recomputes a metric, so the numbers in the files are exactly Metrics/RunResult.
// Output is deterministic: same input -> byte-identical text.

// One completed experiment run.
struct ExperimentRow {
    std::string workload;          // e.g. "fork_join_100"
    std::size_t compute_units = 1; // P
    Policy policy = Policy::fifo;
    Distribution distribution = Distribution::central;
    Metrics metrics;               // computed by Divyaa's compute_metrics
    std::vector<Tick> busy_ticks;  // per-unit useful busy time from RunResult
};

// Quotes a CSV field when it contains a comma, quote or newline (quotes doubled);
// otherwise returns it unchanged.
std::string csv_escape(const std::string& text);

// "fifo" / "sjf" / "cp"; "central" / "static" / "stealing"; and the combined
// label used in charts and the CSV "config" column, e.g. "central_sjf", "static_fifo".
std::string policy_name(Policy policy);
std::string distribution_name(Distribution distribution);
std::string config_label(Policy policy, Distribution distribution);

// CSV text. Header:
// workload,compute_units,policy,distribution,config,makespan,mean_ready_wait,
// p95_ready_wait,mean_turnaround,utilization,busy_time_cv
// Missing metrics (std::nullopt) are written as N/A; doubles use 6 decimals.
// Fields containing a comma, quote or newline are quoted with doubled quotes.
std::string results_csv(const std::vector<ExperimentRow>& rows);

// One bar. value must be finite and >= 0.
struct Bar { std::string label; double value = 0.0; };

// Generic vertical bar chart as a standalone SVG document. Bar heights are
// value / max(values) of a fixed plot height; text is XML-escaped. Throws
// std::invalid_argument for a negative or non-finite value. No bars -> a valid
// SVG that says "no data".
std::string svg_bar_chart(const std::string& title, const std::string& y_axis_label,
                          const std::vector<Bar>& bars);

// Chart 1: makespan comparison, one bar per row labeled "<config> P=<n>".
// Callers pass the rows to compare (normally one workload) so bars are comparable.
std::string makespan_chart_svg(const std::vector<ExperimentRow>& rows, const std::string& title);

// Chart 2: useful busy ticks of each compute unit for one run ("Unit 0", ...).
std::string busy_time_chart_svg(const ExperimentRow& row);

// Writes text to path, replacing any existing file. The parent directory must
// already exist. Throws std::runtime_error naming the path if it cannot be written.
void write_text_file(const std::string& path, const std::string& content);

// ---- one-call bundle for the experiment driver ----

struct ResultFileOptions {
    // Per-unit busy-time charts are written only for rows whose workload name starts
    // with this prefix (they are informative for skew workloads; one per row would
    // otherwise create hundreds of files). Empty prefix = a chart for every row.
    std::string busy_chart_workload_prefix = "owner_skew";
};

// The files to write as {file name, content}, in this order:
//   1. results.csv                         (all rows, results_csv format)
//   2. makespan_<workload>_P<P>.svg        one per distinct (workload, P) group, bars = its rows
//   3. busy_<workload>_<config>_P<P>.svg   one per row selected by the options prefix
// Names are sanitized to letters, digits and underscore. Throws std::invalid_argument
// if two files would get the same name (e.g. two identical rows), so nothing is silently lost.
std::vector<std::pair<std::string, std::string>> result_files(const std::vector<ExperimentRow>& rows,
                                                              const ResultFileOptions& options = {});

// Writes every file of result_files() to path_prefix + name (e.g. "results/"), replacing
// existing files. The directory must already exist.
void write_result_files(const std::vector<ExperimentRow>& rows, const std::string& path_prefix,
                        const ResultFileOptions& options = {});

} // namespace gpu
