// Tests for the data-structure counter export. The counters are measured by running
// the scheduler classes on the owner-skew workload (via the test-only mini simulator)
// at two sizes, and compared with values derived by hand from the RingDeque growth rule:
// capacity starts at 8 and doubles whenever a push finds the deque full.
#include "gpu/ds_metrics.hpp"
#include "gpu/scheduler.hpp"
#include "gpu/self_test.hpp"
#include "oracle_sim.hpp"
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

// Runs one configuration on owner_skew(n, p) and returns its counter rows.
std::vector<DsMetricsRow> measure(std::size_t n, std::size_t p, Policy policy, Distribution distribution) {
    const RunConfig config{p, policy, distribution};
    auto scheduler = make_scheduler(config);
    oracle::run_tasks(oracle::owner_skew_tasks(n, p), *scheduler, p);
    return collect_ds_metrics(*scheduler, "owner_skew_N" + std::to_string(n) + "_P" + std::to_string(p), config);
}

void central_fifo_counters_two_sizes() {
    // All N tasks are enqueued at t=0 in one batch. Growth from capacity 8:
    //   N=100:  pushes #9,17,33,65 trigger growth -> 4 resizes, copies 8+16+32+64 = 120.
    //   N=1000: pushes #9,17,33,65,129,257,513 -> 7 resizes, copies 8+16+...+512 = 1016.
    struct Case { std::size_t n, resizes, copies; };
    for (Case c : {Case{100, 4, 120}, Case{1000, 7, 1016}}) {
        const auto rows = measure(c.n, 2, Policy::fifo, Distribution::central);
        require(rows.size() == 1 && rows[0].structure == "RingDeque" &&
                rows[0].instance == "central_ready_queue" && rows[0].config == "central_fifo", "row identity");
        require(*rows[0].pushes == c.n && *rows[0].pops == c.n && *rows[0].peak_size == c.n,
                "every task pushed once, popped once; peak queue = N");
        require(*rows[0].resizes == c.resizes && *rows[0].elements_copied == c.copies, "growth counters");
        require(!rows[0].comparisons && !rows[0].swaps && !rows[0].searches, "inapplicable counters are N/A");
    }
}
void heap_counters_for_sjf_and_cp() {
    for (Policy policy : {Policy::shortest_job_first, Policy::critical_path})
        for (std::size_t n : {100u, 1000u}) {
            const auto rows = measure(n, 4, policy, Distribution::central);
            require(rows.size() == 1 && rows[0].structure == "BinaryHeap", "one heap row");
            require(*rows[0].pushes == n && *rows[0].pops == n && *rows[0].peak_size == n, "heap push/pop/peak");
            // Sifting a heap of H items costs at most about 2*log2(H) comparisons per op.
            require(*rows[0].comparisons > 0 && *rows[0].comparisons <= 4 * n * 11, "comparison count plausible");
            require(*rows[0].swaps <= *rows[0].comparisons, "swaps cannot exceed comparisons");
            require(!rows[0].resizes && !rows[0].elements_copied, "deque counters are N/A for a heap");
        }
}
void static_counters_per_unit() {
    // P=4: owner of task i is i % 4, so every unit owns exactly N/4 tasks.
    //   N=100:  25 per unit: pushes #9,17 trigger growth -> 2 resizes, copies 8+16 = 24.
    //   N=1000: 250 per unit: pushes #9,17,33,65,129 -> 5 resizes, copies 8+16+32+64+128 = 248.
    struct Case { std::size_t n, per_unit, resizes, copies; };
    for (Case c : {Case{100, 25, 2, 24}, Case{1000, 250, 5, 248}}) {
        const auto rows = measure(c.n, 4, Policy::fifo, Distribution::static_round_robin);
        require(rows.size() == 4, "one row per unit");
        for (std::size_t u = 0; u < 4; ++u) {
            require(rows[u].instance == "unit_" + std::to_string(u) + "_queue" && rows[u].config == "static_fifo",
                    "row identity");
            require(*rows[u].pushes == c.per_unit && *rows[u].pops == c.per_unit && *rows[u].peak_size == c.per_unit,
                    "per-unit counts");
            require(*rows[u].resizes == c.resizes && *rows[u].elements_copied == c.copies, "per-unit growth");
        }
    }
}
void stealing_counters() {
    for (std::size_t n : {100u, 1000u}) {
        const auto rows = measure(n, 4, Policy::fifo, Distribution::work_stealing);
        require(rows.size() == 5 && rows[4].structure == "WorkStealing" && rows[4].config == "stealing_fifo",
                "four unit queues plus a steal row");
        std::size_t pushes = 0, pops = 0;
        for (std::size_t u = 0; u < 4; ++u) {
            require(*rows[u].pushes == n / 4, "each owner queue receives N/4 tasks");
            pushes += *rows[u].pushes;
            pops += *rows[u].pops;
        }
        require(pushes == n && pops == n, "every task pushed once and popped exactly once (front or back)");
        const auto& steal = rows[4];
        require(*steal.steals > 0, "skew workload must trigger steals");
        require(*steal.steals <= *steal.searches && *steal.victim_probes <= *steal.searches * 3 &&
                *steal.victim_probes >= *steal.steals, "search/probe/steal relations (P-1 = 3 probes max)");
        require(!steal.pushes && !steal.resizes, "queue counters are N/A on the steal row");
    }
}
void csv_exact_text() {
    DsMetricsRow deque;
    deque.workload = "w"; deque.config = "central_fifo"; deque.compute_units = 2;
    deque.structure = "RingDeque"; deque.instance = "central_ready_queue";
    deque.pushes = 10; deque.pops = 10; deque.peak_size = 10; deque.resizes = 1; deque.elements_copied = 8;
    DsMetricsRow heap;
    heap.workload = "w"; heap.config = "central_sjf"; heap.compute_units = 2;
    heap.structure = "BinaryHeap"; heap.instance = "central_ready_queue";
    heap.pushes = 3; heap.pops = 3; heap.peak_size = 3; heap.comparisons = 7; heap.swaps = 2;
    DsMetricsRow steal;
    steal.workload = "w"; steal.config = "stealing_fifo"; steal.compute_units = 2;
    steal.structure = "WorkStealing"; steal.instance = "steal_search";
    steal.searches = 4; steal.victim_probes = 6; steal.steals = 2;
    const std::string expected =
        "workload,config,compute_units,structure,instance,pushes,pops,peak_size,resizes,"
        "elements_copied,comparisons,swaps,searches,victim_probes,steals\n"
        "w,central_fifo,2,RingDeque,central_ready_queue,10,10,10,1,8,N/A,N/A,N/A,N/A,N/A\n"
        "w,central_sjf,2,BinaryHeap,central_ready_queue,3,3,3,N/A,N/A,7,2,N/A,N/A,N/A\n"
        "w,stealing_fifo,2,WorkStealing,steal_search,N/A,N/A,N/A,N/A,N/A,N/A,N/A,4,6,2\n";
    require(ds_metrics_csv({deque, heap, steal}) == expected, "CSV text differs from hand-written expectation");
    require(ds_metrics_csv({}).find('\n') + 1 == ds_metrics_csv({}).size(), "header only when empty");
}
void unknown_scheduler_rejected() {
    struct Custom : Scheduler {
        void enqueue_ready(const std::vector<ReadyKey>&) override {}
        std::vector<Assignment> dispatch(const std::vector<UnitId>&) override { return {}; }
    } custom;
    throws<std::invalid_argument>([&] { collect_ds_metrics(custom, "w", RunConfig{}); });
}
} // namespace

std::vector<std::pair<const char*, std::function<void()>>> ds_metrics_tests() {
    return {
        {"ds metrics: central FIFO deque counters at N=100 and N=1000", central_fifo_counters_two_sizes},
        {"ds metrics: SJF and CP heap counters at two sizes", heap_counters_for_sjf_and_cp},
        {"ds metrics: static FIFO per-unit deque counters", static_counters_per_unit},
        {"ds metrics: stealing counters and relations", stealing_counters},
        {"ds metrics: CSV matches hand-written text", csv_exact_text},
        {"ds metrics: unknown scheduler type rejected", unknown_scheduler_rejected},
    };
}
} // namespace gpu
