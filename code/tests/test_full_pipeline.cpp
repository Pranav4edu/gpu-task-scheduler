#include "gpu/contracts.hpp"
#include "gpu/experiments.hpp"
#include "gpu/export.hpp"
#include "gpu/parser_stats.hpp"
#include "gpu/self_test.hpp"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace gpu {
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class Action> void rejects(Action action) {
    bool threw = false;
    try { action(); } catch (const std::exception&) { threw = true; }
    require(threw, "invalid pipeline input was accepted");
}
struct TempInput {
    std::string path = "results/full_pipeline_test.csv";
    explicit TempInput(const std::string& csv) { write_text_file(path, csv); }
    ~TempInput() { std::remove(path.c_str()); }
};
void csv_to_metrics_and_export() {
    const auto w = load_and_validate("data/fixtures/six_tasks.csv");
    std::vector<ExperimentRow> rows;
    for (Policy policy : {Policy::fifo, Policy::shortest_job_first, Policy::critical_path}) {
        const RunConfig config{2, policy, Distribution::central};
        const auto result = simulate(w, config);
        verify_result(w, config, result);
        const auto m = compute_metrics(w, result);
        const bool cp = policy == Policy::critical_path;
        require(m.makespan == (cp ? 11 : 13), "full pipeline hand makespan");
        require(std::abs(*m.mean_ready_wait - (cp ? 3.0 : 2.0)/6) < 1e-12, "full pipeline hand wait");
        require(std::abs(*m.utilization - 18.0/(cp ? 22 : 26)) < 1e-12, "full pipeline hand utilization");
        rows.push_back({"six_tasks", 2, policy, Distribution::central, m, result.busy_ticks});
    }
    const auto files = result_files(rows);
    require(files.size() == 2 && files[0].second.find("central_cp,11,") != std::string::npos,
            "measured results did not reach CSV export");
}
void release_and_predecessor_handoffs() {
    // C is on the first CSV row and has two later-row predecessors. D competes
    // at C's release. Exercise release before, at and after the last completion.
    for (Tick release : {3, 7, 10}) {
        TempInput input("task_id,release_time,duration,dependencies\nC," + std::to_string(release) +
                        ",2,A;B\nA,0,3,\nB,0,7,\nD,10,1,\n");
        const auto w = load_and_validate(input.path);
        for (auto distribution : {Distribution::central, Distribution::static_round_robin,
                                  Distribution::work_stealing}) {
            const RunConfig config{2, Policy::fifo, distribution};
            const auto result = simulate(w, config);
            verify_result(w, config, result);
            const Tick expected = std::max(release, std::max(result.tasks[1].finish, result.tasks[2].finish));
            require(result.tasks[0].ready == expected, "release/predecessor readiness mismatch");
            require(result.tasks[0].start >= expected, "successor started before eligible");
            require(compute_metrics(w, result).utilization.has_value(), "missing integrated metrics");
        }
    }
}
void empty_pipeline() {
    const auto w = load_and_validate("data/fixtures/empty.csv");
    const RunConfig c{8, Policy::fifo, Distribution::work_stealing};
    const auto result = simulate(w, c);
    verify_result(w, c, result);
    const auto m = compute_metrics(w, result);
    require(result.tasks.empty() && m.makespan == 0 && !m.utilization && !m.mean_ready_wait, "empty pipeline");
}
void invalid_csv_and_graph_are_rejected() {
    rejects([] { load_and_validate("data/fixtures/invalid_cycle.csv"); });
    for (const std::string rows : {
            "A,0,1,X\n", "A,0,1,\nA,0,2,\n", "A,0,1,A\n",
            "A,0,9223372036854775807,\nB,0,1,A\n"}) {
        TempInput input("task_id,release_time,duration,dependencies\n" + rows);
        rejects([&] { load_and_validate(input.path); });
    }
}
void preprocessing_counters() {
    ParserStats stats;
    const auto parsed = parse_csv("data/fixtures/six_tasks.csv", &stats);
    require(parsed.tasks.size() == 6 && parsed.edges.size() == 5, "six-task dimensions");
    require(stats.task_ids.inserts == 6 && stats.task_ids.finds == 5 &&
            stats.dependency_pairs.inserts == 5, "parser hash calls do not match rows and dependencies");
    IdHashTable collisions(8, [](const std::string&) { return std::size_t{0}; });
    collisions.insert("a", 1); collisions.insert("b", 2); collisions.insert("a", 3);
    require(collisions.find("a") == 1 && !collisions.find("c"), "collision test mapping");
    const auto s = collisions.stats();
    require(s.inserts == 3 && s.finds == 2 && s.key_comparisons == 7, "hash comparison accounting");
    collisions.reserve(100);
    require(collisions.stats().rehashes == 1 && collisions.stats().entries_rehashed == 2, "rehash accounting");
}
void saved_generators_and_suite() {
    std::size_t workloads = 0, runs = 0;
    for (const auto& input : default_experiment_inputs()) {
        const auto parsed = parse_csv(input.path);
        const auto w = validate_workload(parsed);
        require(w.tasks.size() == input.tasks, "saved workload size");
        if (input.family != "owner_skew") {
            std::ifstream in(input.path, std::ios::binary);
            std::ostringstream text; text << in.rdbuf();
            require(text.str() == workload_csv(generate_experiment_input(input)), "saved generator input changed");
            runs += input.units.size() * 5;
        } else runs += input.units.size() * 3;
        ++workloads;
    }
    require(workloads == 48 && runs == 756, "default experiment matrix changed");
}
void metrics_reject_negative_ready_before_subtraction() {
    const auto w = load_and_validate("data/fixtures/six_tasks.csv");
    auto r = simulate(w, {2, Policy::fifo, Distribution::central});
    r.tasks[0].ready = std::numeric_limits<Tick>::min();
    rejects([&] { compute_metrics(w, r); });
}
}
std::vector<std::pair<const char*, std::function<void()>>> full_pipeline_tests() {
    return {
        {"full CSV graph engine metrics export matches hand oracle", csv_to_metrics_and_export},
        {"full pipeline forward references and competing releases", release_and_predecessor_handoffs},
        {"full pipeline empty workload", empty_pipeline},
        {"full pipeline rejects invalid CSV cycles and rank overflow", invalid_csv_and_graph_are_rejected},
        {"parser hash counters and forced collision accounting", preprocessing_counters},
        {"saved input generators and 756-run experiment matrix", saved_generators_and_suite},
        {"metrics reject negative ready time before subtraction", metrics_reject_negative_ready_before_subtraction}
    };
}
}
