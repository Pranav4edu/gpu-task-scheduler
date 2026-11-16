// Tests for the export module (CSV + SVG). Metric values in these tests are
// FORMATTING inputs chosen by hand, not measured project results. Expected text is
// worked out by hand in the comments.
#include "gpu/export.hpp"
#include "gpu/self_test.hpp"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace gpu {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Exception, class Action>
void throws(Action action) {
    try { action(); } catch (const Exception&) { return; }
    throw std::runtime_error("expected exception was not thrown");
}
bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}
std::size_t count_of(const std::string& text, const std::string& part) {
    std::size_t n = 0;
    for (std::size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + 1)) ++n;
    return n;
}
bool ends_with_svg_close(const std::string& svg) {
    const std::string tail = "</svg>\n";
    return svg.size() >= tail.size() && svg.compare(svg.size() - tail.size(), tail.size(), tail) == 0;
}
ExperimentRow row(const std::string& workload, std::size_t units, Policy policy,
                  Distribution distribution, Tick makespan, std::vector<Tick> busy = {}) {
    ExperimentRow r;
    r.workload = workload;
    r.compute_units = units;
    r.policy = policy;
    r.distribution = distribution;
    r.metrics.makespan = makespan;
    r.busy_ticks = std::move(busy);
    return r;
}

void names_and_labels() {
    require(policy_name(Policy::fifo) == "fifo" && policy_name(Policy::shortest_job_first) == "sjf" &&
            policy_name(Policy::critical_path) == "cp", "policy names");
    require(distribution_name(Distribution::central) == "central" &&
            distribution_name(Distribution::static_round_robin) == "static" &&
            distribution_name(Distribution::work_stealing) == "stealing", "distribution names");
    require(config_label(Policy::shortest_job_first, Distribution::central) == "central_sjf", "label sjf");
    require(config_label(Policy::fifo, Distribution::static_round_robin) == "static_fifo", "label static");
    throws<std::invalid_argument>([] { policy_name(static_cast<Policy>(99)); });
    throws<std::invalid_argument>([] { distribution_name(static_cast<Distribution>(99)); });
}
void csv_exact_text() {
    ExperimentRow a = row("six_tasks", 2, Policy::fifo, Distribution::central, 13);
    a.metrics.mean_ready_wait = 2.0 / 6.0;       // 0.333333
    a.metrics.p95_ready_wait = 1.0;              // 1.000000 (formatting input only)
    a.metrics.mean_turnaround = 43.0 / 6.0;      // 7.166667
    a.metrics.utilization = 18.0 / 26.0;         // 0.692308
    a.metrics.busy_time_cv = 0.25;               // 0.250000
    // Empty workload: makespan 0 and every ratio N/A. Name needs CSV quoting: a,b "x"
    ExperimentRow b = row("a,b \"x\"", 1, Policy::fifo, Distribution::static_round_robin, 0);
    const std::string expected =
        "workload,compute_units,policy,distribution,config,makespan,mean_ready_wait,"
        "p95_ready_wait,mean_turnaround,utilization,busy_time_cv\n"
        "six_tasks,2,fifo,central,central_fifo,13,0.333333,1.000000,7.166667,0.692308,0.250000\n"
        "\"a,b \"\"x\"\"\",1,fifo,static,static_fifo,0,N/A,N/A,N/A,N/A,N/A\n";
    require(results_csv({a, b}) == expected, "CSV text differs from hand-written expectation");
}
void csv_header_only_when_empty() {
    const std::string text = results_csv({});
    require(count_of(text, "\n") == 1 && contains(text, "workload,compute_units"), "empty CSV");
}
void svg_makespan_chart_geometry() {
    // Values 13 and 11, plot height 300: bar 0 = 300.00 tall; bar 1 = 11/13*300 = 253.85.
    // Layout: left 70, slot 80, bar 50 -> x = 85 and 165; base y = 350 -> y = 50.00 and 96.15.
    auto rows = std::vector<ExperimentRow>{
        row("six_tasks", 2, Policy::fifo, Distribution::central, 13),
        row("six_tasks", 2, Policy::critical_path, Distribution::central, 11)};
    const std::string svg = makespan_chart_svg(rows, "Makespan, six tasks");
    require(svg.rfind("<svg ", 0) == 0 && ends_with_svg_close(svg), "not a standalone svg document");
    require(count_of(svg, "<rect") == 3, "background plus two bars expected");
    require(contains(svg, "<rect x=\"85.00\" y=\"50.00\" width=\"50.00\" height=\"300.00\""), "bar 0 geometry");
    require(contains(svg, "<rect x=\"165.00\" y=\"96.15\" width=\"50.00\" height=\"253.85\""), "bar 1 geometry");
    require(contains(svg, ">central_fifo P=2<") && contains(svg, ">central_cp P=2<"), "bar labels");
    require(contains(svg, ">13<") && contains(svg, ">11<"), "value labels");
    require(contains(svg, "Makespan, six tasks") && contains(svg, "makespan (ticks)"), "titles");
}
void svg_busy_chart_geometry() {
    // Six-task static trace: unit 0 runs A,C,E = 2+6+4 = 12; unit 1 runs B,D,F = 3+2+1 = 6.
    // Heights: 12 -> 300.00, 6 -> 150.00.
    const std::string svg = busy_time_chart_svg(
        row("six_tasks", 2, Policy::fifo, Distribution::static_round_robin, 13, {12, 6}));
    require(count_of(svg, "<rect") == 3, "two bars plus background");
    require(contains(svg, "height=\"300.00\"") && contains(svg, "height=\"150.00\""), "busy heights");
    require(contains(svg, ">Unit 0<") && contains(svg, ">Unit 1<"), "unit labels");
    require(contains(svg, "six_tasks") && contains(svg, "static_fifo") && contains(svg, "P=2"), "chart title");
}
void svg_escapes_text() {
    const std::string svg = svg_bar_chart("A & B <x> \"q\"", "y", {Bar{"<&>", 1.0}});
    require(contains(svg, "A &amp; B &lt;x&gt; &quot;q&quot;"), "title not escaped");
    require(contains(svg, "&lt;&amp;&gt;"), "label not escaped");
    require(!contains(svg, "<x>") && !contains(svg, "<&>"), "raw markup leaked into svg");
}
void svg_degenerate_inputs() {
    const std::string none = svg_bar_chart("t", "y", {});
    require(contains(none, "no data") && count_of(none, "<rect") == 1 && ends_with_svg_close(none),
            "empty chart");
    const std::string zeros = svg_bar_chart("t", "y", {Bar{"a", 0.0}, Bar{"b", 0.0}});
    require(contains(zeros, "height=\"0.00\"") && !contains(zeros, "nan") && !contains(zeros, "inf"),
            "all-zero chart must not divide by zero");
    require(contains(makespan_chart_svg({}, "t"), "no data"), "no rows");
}
void svg_rejects_bad_values() {
    throws<std::invalid_argument>([] { svg_bar_chart("t", "y", {Bar{"a", -1.0}}); });
    throws<std::invalid_argument>([] { svg_bar_chart("t", "y", {Bar{"a", std::nan("")}}); });
    throws<std::invalid_argument>([] {
        svg_bar_chart("t", "y", {Bar{"a", std::numeric_limits<double>::infinity()}}); });
}
void output_is_deterministic() {
    auto rows = std::vector<ExperimentRow>{
        row("w", 4, Policy::shortest_job_first, Distribution::central, 7, {3, 4, 0, 1})};
    require(results_csv(rows) == results_csv(rows), "CSV not deterministic");
    require(makespan_chart_svg(rows, "t") == makespan_chart_svg(rows, "t"), "svg not deterministic");
    require(busy_time_chart_svg(rows[0]) == busy_time_chart_svg(rows[0]), "busy svg not deterministic");
}
std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}
void file_round_trip_and_errors() {
    // Written to the current directory and removed afterwards (no std::filesystem:
    // it can fail to load on Windows/Git Bash toolchains).
    const std::string file = "export_test_tmp.txt";
    const std::string text = "a,b\n1,2\n";
    write_text_file(file, text);
    require(read_file(file) == text, "file content differs");
    write_text_file(file, "x\n");                       // overwrites instead of appending
    require(read_file(file) == "x\n", "file must be truncated on rewrite");
    std::remove(file.c_str());
    // A missing parent directory is an error that names the path.
    try {
        write_text_file("no_such_directory_for_export_test/out.txt", "nope");
        throw std::logic_error("write into a missing directory should have failed");
    } catch (const std::runtime_error& e) {
        require(std::string(e.what()).find("no_such_directory_for_export_test") != std::string::npos,
                "error must name the path");
    }
}
void result_bundle_names_and_contents() {
    // Two owner-skew rows (same workload and P) + one chain row.
    auto central = row("owner_skew_N100_P2", 2, Policy::fifo, Distribution::central, 280, {275, 275});
    auto stat = row("owner_skew_N100_P2", 2, Policy::fifo, Distribution::static_round_robin, 500, {500, 50});
    auto chain = row("chain_100", 1, Policy::fifo, Distribution::central, 700, {700});
    const std::vector<ExperimentRow> rows = {central, stat, chain};
    const auto files = result_files(rows);
    // results.csv, then one makespan chart per (workload, P) group, then busy charts for
    // owner_skew rows only (chain_100 does not match the default prefix).
    require(files.size() == 5, "expected csv + 2 makespan + 2 busy files");
    require(files[0].first == "results.csv" && files[0].second == results_csv(rows), "results.csv");
    require(files[1].first == "makespan_owner_skew_N100_P2_P2.svg" &&
            files[1].second == makespan_chart_svg({central, stat}, "Makespan - owner_skew_N100_P2, P=2"),
            "owner-skew makespan chart groups both configs");
    require(count_of(files[1].second, "<rect") == 3, "two bars in the grouped chart");
    require(files[2].first == "makespan_chain_100_P1.svg" && count_of(files[2].second, "<rect") == 2, "chain chart");
    require(files[3].first == "busy_owner_skew_N100_P2_central_fifo_P2.svg" &&
            files[3].second == busy_time_chart_svg(central), "busy chart central");
    require(files[4].first == "busy_owner_skew_N100_P2_static_fifo_P2.svg", "busy chart static");
    // Empty prefix: a busy chart for every row.
    ResultFileOptions everything;
    everything.busy_chart_workload_prefix = "";
    require(result_files(rows, everything).size() == 6, "empty prefix selects every row");
    // File names are sanitized.
    require(result_files({row("a b/c", 1, Policy::fifo, Distribution::central, 1)})[1].first ==
            "makespan_a_b_c_P1.svg", "name sanitizing");
    // Two identical rows would overwrite each other's busy chart: refuse.
    throws<std::invalid_argument>([&] { result_files({central, central}); });
}
void result_bundle_preserves_multiple_unit_counts() {
    const auto single = row("chain_100", 1, Policy::fifo, Distribution::central, 700, {700});
    const auto dual = row("chain_100", 2, Policy::fifo, Distribution::central, 700, {350, 350});
    const std::vector<ExperimentRow> rows = {single, dual};
    ResultFileOptions options;
    options.busy_chart_workload_prefix = "";
    const auto files = result_files(rows, options);
    require(files.size() == 5, "expected csv + 2 makespan + 2 busy files across unit counts");
    require(files[3].first == "busy_chain_100_central_fifo_P1.svg" &&
            files[3].second == busy_time_chart_svg(single), "single-unit busy chart preserved");
    require(files[4].first == "busy_chain_100_central_fifo_P2.svg" &&
            files[4].second == busy_time_chart_svg(dual), "two-unit busy chart preserved");
    const std::string prefix = "export_multi_units_test_";
    write_result_files(rows, prefix, options);
    for (const auto& file : files) {
        require(read_file(prefix + file.first) == file.second, "multi-unit bundle content differs");
        std::remove((prefix + file.first).c_str());
    }
}
void result_bundle_writes_files() {
    const auto rows = std::vector<ExperimentRow>{
        row("owner_skew_N100_P2", 2, Policy::fifo, Distribution::central, 280, {275, 275})};
    const std::string prefix = "export_bundle_test_";
    write_result_files(rows, prefix);
    for (const auto& file : result_files(rows)) {
        require(read_file(prefix + file.first) == file.second, "written file content differs");
        std::remove((prefix + file.first).c_str());
    }
}
} // namespace

std::vector<std::pair<const char*, std::function<void()>>> export_tests() {
    return {
        {"export policy and configuration names", names_and_labels},
        {"export CSV matches hand-written text (N/A and quoting)", csv_exact_text},
        {"export CSV with no rows is header only", csv_header_only_when_empty},
        {"export makespan chart geometry", svg_makespan_chart_geometry},
        {"export busy-time chart geometry", svg_busy_chart_geometry},
        {"export SVG escapes titles and labels", svg_escapes_text},
        {"export SVG handles empty and all-zero data", svg_degenerate_inputs},
        {"export SVG rejects negative and non-finite values", svg_rejects_bad_values},
        {"export output is deterministic", output_is_deterministic},
        {"export file write round trip and errors", file_round_trip_and_errors},
        {"export result bundle names, grouping and sanitizing", result_bundle_names_and_contents},
        {"export result bundle preserves multiple unit counts", result_bundle_preserves_multiple_unit_counts},
        {"export result bundle writes files", result_bundle_writes_files},
    };
}
} // namespace gpu
