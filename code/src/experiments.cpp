#include "gpu/experiments.hpp"
#include "gpu/ds_metrics.hpp"
#include "gpu/export.hpp"
#include "gpu/parser_stats.hpp"
#include "gpu/scheduler.hpp"
#include "gpu/simulator.hpp"
#include "gpu/workload_generators.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace gpu {
namespace {
std::uint32_t next_random(std::uint32_t& state) {
    state = state * 1664525u + 1013904223u; // specified unsigned 32-bit arithmetic
    return state;
}
const std::vector<RunConfig> modes = {
    {1, Policy::fifo, Distribution::central},
    {1, Policy::shortest_job_first, Distribution::central},
    {1, Policy::critical_path, Distribution::central},
    {1, Policy::fifo, Distribution::static_round_robin},
    {1, Policy::fifo, Distribution::work_stealing}
};
void trace(std::ostream& out, const std::string& name, const ValidatedWorkload& w,
           const RunConfig& config, const RunResult& result) {
    for (const auto& t : result.tasks)
        out << name << ',' << config.compute_units << ',' << config_label(config.policy, config.distribution)
            << ',' << t.task << ',' << csv_escape(w.tasks[t.task].external_id) << ',' << t.ready << ','
            << t.start << ',' << t.finish << ',' << t.unit << ',' << t.initial_owner << ',' << t.stolen << '\n';
}
void hash_row(std::ostream& out, const std::string& name, const char* instance, const HashStats& s) {
    out << name << ",IdHashTable," << instance << ",N/A,N/A," << s.inserts << ',' << s.finds
        << ',' << s.key_comparisons << ',' << s.rehashes << ',' << s.entries_rehashed << '\n';
}
}

std::vector<ExperimentInput> default_experiment_inputs() {
    std::vector<ExperimentInput> inputs;
    for (std::size_t n : {100u, 1000u, 10000u}) {
        for (const std::string family : {"chain", "fork_join", "mixed", "layered"}) {
            const std::vector<std::uint32_t> seeds = (family == "mixed" || family == "layered")
                ? std::vector<std::uint32_t>{101, 202, 303, 404, 505} : std::vector<std::uint32_t>{0};
            for (auto seed : seeds) {
                const std::string name = family + "_N" + std::to_string(n) +
                    (seed ? "_S" + std::to_string(seed) : "");
                inputs.push_back({name, "data/workloads/" + name + ".csv", family, n, seed, {1,2,4,8}});
            }
        }
        for (std::size_t p : {1u,2u,4u,8u}) {
            const std::string name = "owner_skew_N" + std::to_string(n) + "_P" + std::to_string(p);
            inputs.push_back({name, "data/workloads/" + name + ".csv", "owner_skew", n, 0, {p}});
        }
    }
    return inputs;
}

ParsedInput generate_experiment_input(const ExperimentInput& input) {
    if (input.family == "chain") return make_chain_workload(input.tasks, 0, 3);
    if (input.family == "fork_join") {
        if (input.tasks < 3) throw std::invalid_argument("fork-join needs at least 3 tasks");
        return make_fork_join_workload(input.tasks - 2, 0, 3);
    }
    if (input.family != "mixed" && input.family != "layered")
        throw std::invalid_argument("generator supports chain, fork_join, mixed and layered");
    ParsedInput result;
    std::uint32_t state = input.seed;
    for (TaskId i = 0; i < input.tasks; ++i) {
        const Tick duration = 1 + next_random(state) % 100;
        const Tick release = (i / 25 % 4) * 20; // staggered, deterministic release bursts
        result.tasks.push_back({"T" + std::to_string(i), release, duration});
        if (input.family == "layered" && i >= 10) {
            // Ten tasks per layer, up to three distinct predecessors in the previous layer.
            const TaskId previous = (i / 10 - 1) * 10;
            std::vector<TaskId> chosen;
            for (int j = 0; j < 3; ++j) {
                const TaskId pred = previous + next_random(state) % 10;
                if (std::find(chosen.begin(), chosen.end(), pred) == chosen.end()) {
                    chosen.push_back(pred);
                    result.edges.push_back({pred, i});
                }
            }
        }
    }
    return result;
}

std::string workload_csv(const ParsedInput& input) {
    std::vector<std::vector<TaskId>> predecessors(input.tasks.size());
    for (const auto& e : input.edges) {
        if (e.from >= input.tasks.size() || e.to >= input.tasks.size())
            throw std::invalid_argument("generator edge out of bounds");
        predecessors[e.to].push_back(e.from);
    }
    std::ostringstream out;
    out << "task_id,release_time,duration,dependencies\n";
    for (TaskId i = 0; i < input.tasks.size(); ++i) {
        const auto& task = input.tasks[i];
        out << task.external_id << ',' << task.release << ',' << task.duration << ',';
        for (std::size_t j = 0; j < predecessors[i].size(); ++j) {
            if (j) out << ';';
            out << input.tasks[predecessors[i][j]].external_id;
        }
        out << '\n';
    }
    return out.str();
}

void write_experiment_inputs() {
    for (const auto& input : default_experiment_inputs())
        if (input.family != "owner_skew") write_text_file(input.path, workload_csv(generate_experiment_input(input)));
}

void run_experiments() {
    std::vector<ExperimentRow> rows;
    std::vector<DsMetricsRow> ds_rows;
    std::ostringstream per_unit, preprocessing, manifest, engine, example_trace, summary;
    per_unit << "workload,compute_units,config,unit,busy_ticks\n";
    preprocessing << "workload,structure,instance,vertices,edges,inserts,finds,key_comparisons,rehashes,entries_rehashed\n";
    manifest << "workload,input_file,family,seed,tasks,edges,compute_units,config\n";
    engine << "workload,compute_units,config,timestamp_batches,successor_visits,ready_tasks,dispatch_calls\n";
    example_trace << "workload,compute_units,config,task,external_id,ready,start,finish,unit,initial_owner,stolen\n";
    std::size_t loaded = 0;
    for (const auto& input : default_experiment_inputs()) {
        ParserStats parser_stats;
        const auto w = validate_workload(parse_csv(input.path, &parser_stats));
        if (w.tasks.size() != input.tasks) throw std::runtime_error(input.path + ": unexpected task count");
        ++loaded;
        hash_row(preprocessing, input.name, "task_ids", parser_stats.task_ids);
        hash_row(preprocessing, input.name, "dependency_pairs", parser_stats.dependency_pairs);
        preprocessing << input.name << ",DirectedGraph,dependency_graph," << w.graph.vertex_count() << ','
                      << w.graph.edge_count() << ",N/A,N/A,N/A,N/A,N/A\n";
        for (auto p : input.units) for (auto config : modes) {
            // Preserve the plan: skew compares distribution using FIFO, at the same P.
            if (input.family == "owner_skew" && config.policy != Policy::fifo) continue;
            config.compute_units = p;
            auto scheduler = make_scheduler(config);
            EngineStats stats;
            const auto result = simulate_with_scheduler(w, config, *scheduler, &stats);
            verify_result(w, config, result); // mandatory before any benchmark values are accepted
            const auto metric = compute_metrics(w, result);
            rows.push_back({input.name, p, config.policy, config.distribution, metric, result.busy_ticks});
            auto counters = collect_ds_metrics(*scheduler, input.name, config);
            ds_rows.insert(ds_rows.end(), counters.begin(), counters.end());
            DsMetricsRow event_row;
            event_row.workload = input.name; event_row.config = config_label(config.policy, config.distribution);
            event_row.compute_units = p; event_row.structure = "BinaryHeap"; event_row.instance = "event_queue";
            event_row.pushes = stats.events.pushes; event_row.pops = stats.events.pops;
            event_row.peak_size = stats.events.peak_size; event_row.comparisons = stats.events.comparisons;
            event_row.swaps = stats.events.swaps; ds_rows.push_back(event_row);
            const auto label = config_label(config.policy, config.distribution);
            for (UnitId u = 0; u < p; ++u)
                per_unit << input.name << ',' << p << ',' << label << ',' << u << ',' << result.busy_ticks[u] << '\n';
            manifest << input.name << ',' << input.path << ',' << input.family << ',' << input.seed << ','
                     << w.tasks.size() << ',' << w.graph.edge_count() << ',' << p << ',' << label << '\n';
            engine << input.name << ',' << p << ',' << label << ',' << stats.timestamp_batches << ','
                   << stats.successor_visits << ',' << stats.ready_tasks << ',' << stats.dispatch_calls << '\n';
        }
        std::cout << "Verified " << input.name << '\n';
    }
    const auto example = load_and_validate("data/fixtures/six_tasks.csv");
    std::vector<ExperimentRow> example_rows;
    for (auto config : modes) {
        config.compute_units = 2;
        const auto result = simulate(example, config);
        verify_result(example, config, result);
        example_rows.push_back({"six_tasks", 2, config.policy, config.distribution,
                               compute_metrics(example, result), result.busy_ticks});
        trace(example_trace, "six_tasks", example, config, result);
    }
    write_result_files(rows, "results/");
    write_text_file("results/metrics.csv", results_csv(rows));
    write_text_file("results/per_unit.csv", per_unit.str());
    write_text_file("results/preprocessing.csv", preprocessing.str());
    write_text_file("results/experiment_manifest.csv", manifest.str());
    write_text_file("results/ds_metrics.csv", ds_metrics_csv(ds_rows));
    write_text_file("results/engine_metrics.csv", engine.str());
    write_text_file("results/trace.csv", example_trace.str());
    write_text_file("results/six_task_metrics.csv", results_csv(example_rows));
    write_text_file("results/makespan_six_tasks_P2.svg", makespan_chart_svg(example_rows, "Six-task hand oracle, P=2"));
    summary << "Group 4 GPU-inspired scheduler: complete deterministic experiment pipeline\n"
            << "Saved workload CSVs loaded: " << loaded << "\nVerified benchmark runs: " << rows.size()
            << "\nMain suite: 36 workloads x 4 unit counts x 5 policies/distributions = 720 runs.\n"
            << "Owner-skew: 12 workloads x 3 FIFO distributions = 36 runs.\n"
            << "Six-task demonstration: 5 additional verified runs, P=2.\n"
            << "Every run uses the production parser/hash table, graph, engine and scheduler.\n"
            << "Metrics use virtual ticks; they are not measured GPU hardware performance.\n"
            << "For a fixed input/configuration the outputs are deterministic.\n"
            << "The saved CSV input is authoritative; see config/README.md for generation settings.\n"
            << "Preprocessing is counted once per input; runtime counters reset each run.\n"
            << "metrics.csv and results.csv contain identical metrics for compatibility.\n";
    write_text_file("results/summary.txt", summary.str());
    // A portable report index; charts and CSVs remain standalone files.
    std::ostringstream html;
    html << "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><title>Group 4 Results</title>"
            "<style>body{font:16px system-ui;max-width:1100px;margin:40px auto;padding:0 24px;color:#173047}"
            "table{border-collapse:collapse;width:100%}td,th{padding:8px;text-align:left;border-bottom:1px solid #dce5eb}"
            "a{color:#126b72}img{width:100%;max-width:900px}</style><h1>GPU task scheduling and load balancing</h1>"
            "<p>48 saved workloads. 756 verified benchmark runs. Virtual compute units; no physical GPU required.</p>"
            "<p><a href=\"metrics.csv\">Metrics CSV</a> | <a href=\"per_unit.csv\">Per-unit busy times</a> | "
            "<a href=\"ds_metrics.csv\">Runtime DS counters</a> | <a href=\"preprocessing.csv\">Preprocessing</a> | "
            "<a href=\"experiment_manifest.csv\">Experiment manifest</a> | <a href=\"trace.csv\">Six-task traces</a></p>"
            "<img src=\"makespan_six_tasks_P2.svg\" alt=\"Six-task makespans\"><h2>Benchmark charts</h2>"
            "<table><tr><th>Workload</th><th>Units</th><th>Makespan comparison</th></tr>";
    for (const auto& input : default_experiment_inputs()) for (auto p : input.units)
        html << "<tr><td>" << input.name << "</td><td>" << p << "</td><td><a href=\"makespan_"
             << input.name << "_P" << p << ".svg\">Open chart</a></td></tr>";
    html << "</table></html>\n";
    write_text_file("results/index.html", html.str());
    std::cout << "Completed " << rows.size() << " verified benchmark runs; open code/results/index.html.\n";
}
} // namespace gpu
