#include "gpu/simulator.hpp"
#include "gpu/self_test.hpp"
#include <algorithm>
#include <fstream>
#include <functional>
#include <limits>
#include <numeric>
#include <ostream>
#include <random>
#include <stdexcept>
#include <tuple>

namespace gpu {
namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class Error, class Action>
void rejects(Action action) {
    try { action(); } catch (const Error&) { return; }
    throw std::runtime_error("expected error was not raised");
}

// Test-only fixtures, NOT implementations of Shaswitha's graph or Aayush's
// scheduler. No test fixture is used by the production simulate entry point.
struct FixtureGraph {
    std::vector<std::vector<TaskId>> successors;
    std::vector<std::size_t> degrees;
    FixtureGraph(std::size_t count, const std::vector<EdgePair>& edges = {})
        : successors(count), degrees(count, 0) {
        for (const auto& edge : edges) {
            successors.at(edge.from).push_back(edge.to);
            ++degrees.at(edge.to);
        }
    }
    DependencyView view() const {
        return {degrees, [this](TaskId id, const SuccessorVisitor& visit) {
            for (TaskId successor : successors.at(id)) visit(successor);
        }};
    }
};

// Deliberately simple vector reference queues. All comparator expressions here
// are independent of gpu/priority.hpp, and used only to exercise the engine.
class ReferenceScheduler : public Scheduler {
public:
    explicit ReferenceScheduler(RunConfig config) : config_(config) {}
    void enqueue_ready(const std::vector<ReadyKey>& batch) override {
        require(std::is_sorted(batch.begin(), batch.end(), [](const auto& a, const auto& b) {
            return a.task < b.task;
        }), "ready batch is not sorted by task ID");
        batches.push_back(batch);
        ready_.insert(ready_.end(), batch.begin(), batch.end());
    }
    std::vector<Assignment> dispatch(const std::vector<UnitId>& idle) override {
        require(std::is_sorted(idle.begin(), idle.end()), "idle units are not sorted");
        if (config_.policy == Policy::shortest_job_first) {
            std::sort(ready_.begin(), ready_.end(), [](const auto& a, const auto& b) {
                return std::tie(a.duration, a.ready_time, a.task) < std::tie(b.duration, b.ready_time, b.task);
            });
        } else if (config_.policy == Policy::critical_path) {
            std::sort(ready_.begin(), ready_.end(), [](const auto& a, const auto& b) {
                if (a.critical_rank != b.critical_rank) return a.critical_rank > b.critical_rank;
                return std::tie(a.ready_time, a.task) < std::tie(b.ready_time, b.task);
            });
        }
        std::vector<Assignment> assignments;
        for (UnitId unit : idle) {
            auto it = ready_.begin();
            if (config_.distribution == Distribution::static_round_robin)
                it = std::find_if(ready_.begin(), ready_.end(), [&](const auto& key) {
                    return key.task % config_.compute_units == unit;
                });
            if (it != ready_.end()) {
                assignments.push_back({it->task, unit, false});
                ready_.erase(it);
            }
        }
        return assignments;
    }
    std::vector<std::vector<ReadyKey>> batches;
private:
    RunConfig config_;
    std::vector<ReadyKey> ready_;
};

class ScriptedScheduler : public Scheduler {
public:
    explicit ScriptedScheduler(std::vector<std::vector<Assignment>> steps) : steps_(std::move(steps)) {}
    void enqueue_ready(const std::vector<ReadyKey>&) override {}
    std::vector<Assignment> dispatch(const std::vector<UnitId>&) override {
        if (step_ == steps_.size()) return {};
        return steps_[step_++];
    }
private:
    std::vector<std::vector<Assignment>> steps_;
    std::size_t step_ = 0;
};

const std::vector<TaskSpec> six_tasks = {
    {"A",0,2},{"B",0,3},{"C",0,6},{"D",0,2},{"E",0,4},{"F",0,1}
};
const std::vector<EdgePair> six_edges = {{0,3},{1,3},{2,4},{3,5},{4,5}};
const std::vector<Tick> six_ranks = {5,6,11,3,5,1};

void same_result(const RunResult& got, const RunResult& expected) {
    require(got.makespan == expected.makespan, "makespan mismatch");
    require(got.busy_ticks == expected.busy_ticks, "per-unit busy ticks mismatch");
    require(got.tasks.size() == expected.tasks.size(), "task record count mismatch");
    for (TaskId id = 0; id < got.tasks.size(); ++id) {
        const auto& a = got.tasks[id]; const auto& b = expected.tasks[id];
        require(std::tie(a.task,a.ready,a.start,a.finish,a.unit,a.initial_owner,a.stolen) ==
                std::tie(b.task,b.ready,b.start,b.finish,b.unit,b.initial_owner,b.stolen),
                "task trace mismatch");
    }
}

void six_task_oracles() {
    FixtureGraph graph(6, six_edges);
    const auto original = graph.degrees;
    for (Policy policy : {Policy::fifo, Policy::shortest_job_first, Policy::critical_path}) {
        RunConfig config{2,policy,Distribution::central};
        ReferenceScheduler scheduler(config);
        EngineStats stats;
        auto got = simulate_events(six_tasks, graph.view(), six_ranks, config, scheduler, &stats);
        RunResult expected;
        if (policy == Policy::critical_path) {
            expected = {{{0,0,3,5,1,1,false},{1,0,0,3,1,1,false},{2,0,0,6,0,0,false},
                         {3,5,5,7,1,1,false},{4,6,6,10,0,0,false},{5,10,10,11,0,0,false}}, {11,7}, 11};
        } else {
            expected = {{{0,0,0,2,0,0,false},{1,0,0,3,1,1,false},{2,0,2,8,0,0,false},
                         {3,3,3,5,1,1,false},{4,8,8,12,0,0,false},{5,12,12,13,0,0,false}}, {13,5}, 13};
        }
        same_result(got, expected);
        require(stats.events.pushes == 12 && stats.events.pops == 12, "not exactly two events per task");
        require(stats.successor_visits == 5 && stats.ready_tasks == 6, "readiness/edge count mismatch");
        require(graph.degrees == original, "engine mutated graph indegrees");
        ReferenceScheduler again(config);
        same_result(simulate_events(six_tasks, graph.view(), six_ranks, config, again), got);
    }
}

// Small, independent O(time * (N+E+P)) oracle: advance one tick, scan all tasks
// and predecessor records. It does not use the event heap or successor counters.
RunResult tick_oracle(const std::vector<TaskSpec>& tasks, const std::vector<EdgePair>& edges,
                      const std::vector<Tick>& ranks, const RunConfig& config) {
    RunResult result;
    result.tasks.resize(tasks.size()); result.busy_ticks.resize(config.compute_units, 0);
    std::vector<int> phase(tasks.size(), 0);
    std::vector<TaskId> ready;
    std::size_t done = 0;
    for (Tick time = 0; done < tasks.size(); ++time) {
        require(time < 1000, "tick oracle exceeded bounded fixture time");
        for (TaskId id = 0; id < tasks.size(); ++id)
            if (phase[id] == 2 && result.tasks[id].finish == time) {
                phase[id] = 3; ++done; result.makespan = time;
            }
        for (TaskId id = 0; id < tasks.size(); ++id) {
            if (phase[id] != 0 || tasks[id].release > time) continue;
            bool eligible = true;
            for (const auto& edge : edges)
                if (edge.to == id && phase[edge.from] != 3) eligible = false;
            if (eligible) {
                phase[id] = 1; ready.push_back(id); result.tasks[id].ready = time;
            }
        }
        if (config.policy != Policy::fifo) {
            std::sort(ready.begin(), ready.end(), [&](TaskId a, TaskId b) {
                if (config.policy == Policy::critical_path && ranks[a] != ranks[b]) return ranks[a] > ranks[b];
                if (config.policy == Policy::shortest_job_first && tasks[a].duration != tasks[b].duration)
                    return tasks[a].duration < tasks[b].duration;
                return std::tie(result.tasks[a].ready,a) < std::tie(result.tasks[b].ready,b);
            });
        }
        for (UnitId unit = 0; unit < config.compute_units; ++unit) {
            bool busy = false;
            for (TaskId id = 0; id < tasks.size(); ++id)
                if (phase[id] == 2 && result.tasks[id].unit == unit) busy = true;
            if (busy) continue;
            auto it = ready.begin();
            if (config.distribution == Distribution::static_round_robin)
                it = std::find_if(ready.begin(), ready.end(), [&](TaskId id) { return id % config.compute_units == unit; });
            if (it == ready.end()) continue;
            const auto id = *it; ready.erase(it); phase[id] = 2;
            result.tasks[id] = {id,result.tasks[id].ready,time,time+tasks[id].duration,unit,unit,false};
            result.busy_ticks[unit] += tasks[id].duration;
        }
    }
    return result;
}

void randomized_differential() {
    std::mt19937 rng(6320);
    for (int trial = 0; trial < 80; ++trial) {
        const auto count = 1 + rng() % 12;
        std::vector<TaskSpec> tasks;
        std::vector<EdgePair> edges;
        for (TaskId id = 0; id < count; ++id) {
            tasks.push_back({std::to_string(id), static_cast<Tick>(rng()%7), static_cast<Tick>(1+rng()%5)});
            for (TaskId from = 0; from < id; ++from)
                if (rng()%5 == 0) edges.push_back({from,id});
        }
        std::vector<Tick> ranks(count,0);
        for (std::size_t i = count; i > 0; --i) {
            const auto id = i-1;
            Tick tail = 0;
            for (const auto& edge : edges) if (edge.from == id) tail = std::max(tail,ranks[edge.to]);
            ranks[id] = tasks[id].duration + tail;
        }
        // Dense IDs need not be in topological order (CSV forward references).
        std::vector<TaskId> permutation(count);
        std::iota(permutation.begin(),permutation.end(),0);
        std::shuffle(permutation.begin(),permutation.end(),rng);
        auto permuted_tasks=tasks; auto permuted_ranks=ranks;
        for (TaskId id=0; id<count; ++id) {
            permuted_tasks[permutation[id]]=tasks[id];
            permuted_ranks[permutation[id]]=ranks[id];
        }
        tasks=std::move(permuted_tasks); ranks=std::move(permuted_ranks);
        for (auto& edge : edges) { edge.from=permutation[edge.from]; edge.to=permutation[edge.to]; }
        // Reverse adjacency insertion to ensure candidate order cannot leak into FIFO.
        std::reverse(edges.begin(),edges.end());
        FixtureGraph graph(count,edges);
        for (std::size_t units : {1u,2u,3u}) {
            for (int mode = 0; mode < 4; ++mode) {
                RunConfig config{units,mode == 1 ? Policy::shortest_job_first : mode == 2 ? Policy::critical_path : Policy::fifo,
                                 mode == 3 ? Distribution::static_round_robin : Distribution::central};
                ReferenceScheduler scheduler(config);
                same_result(simulate_events(tasks,graph.view(),ranks,config,scheduler), tick_oracle(tasks,edges,ranks,config));
            }
        }
    }
}
} // namespace

int run_engine_tests(std::ostream& output) {
    using Test = std::pair<const char*,std::function<void()>>;
    const std::vector<Test> tests = {
        {"empty workload and fresh statistics", [] {
            FixtureGraph graph(0); ReferenceScheduler scheduler({}); EngineStats stats; stats.ready_tasks=99;
            auto result=simulate_events({},graph.view(),{}, {},scheduler,&stats);
            require(result.tasks.empty() && result.makespan==0 && result.busy_ticks==std::vector<Tick>({0,0}), "empty result");
            require(stats.ready_tasks==0 && stats.events.pushes==0 && stats.dispatch_calls==0, "empty counters");
        }},
        {"six-task FIFO SJF CP hand traces and repeatability", six_task_oracles},
        {"simultaneous completions and arrival form one sorted batch", [] {
            FixtureGraph graph(4,{{2,0},{3,0}}); RunConfig config{2,Policy::fifo,Distribution::central};
            ReferenceScheduler scheduler(config); EngineStats stats;
            auto r=simulate_events({{"join",0,1},{"arrival",2,1},{"left",0,2},{"right",0,2}},graph.view(),{},config,scheduler,&stats);
            require(scheduler.batches.size()==2 && scheduler.batches[1].size()==2, "join queued more than once or timestamp split");
            require(scheduler.batches[1][0].task==0 && scheduler.batches[1][1].task==1, "unstable ready order");
            require(r.tasks[0].ready==2 && r.tasks[0].start==2 && r.tasks[0].unit==0 && r.tasks[1].unit==1, "batch dispatch wrong");
            require(stats.timestamp_batches==3 && stats.ready_tasks==4, "timestamp/readiness count wrong");
        }},
        {"existing FIFO work precedes same-time arrival", [] {
            FixtureGraph graph(3); RunConfig config{1,Policy::fifo,Distribution::central}; ReferenceScheduler s(config);
            auto r=simulate_events({{"a",0,3},{"waiting",0,2},{"new",3,1}},graph.view(),{},config,s);
            require(r.tasks[1].start==3 && r.tasks[2].start==5, "FIFO wait order changed");
        }},
        {"readiness waits for both release and predecessors", [] {
            FixtureGraph graph(2,{{1,0}});
            for (bool late_release : {false,true}) {
                ReferenceScheduler s({});
                auto r=simulate_events({{"dependent",late_release?10:0,2},{"parent",late_release?0:7,3}},graph.view(),{}, {},s);
                require(r.tasks[0].ready==10 && r.tasks[0].start==10 && r.makespan==12, "release/dependency conjunction wrong");
            }
        }},
        {"idle units and idle gaps retain zero busy ticks", [] {
            FixtureGraph graph(2); RunConfig c{4,Policy::fifo,Distribution::central}; ReferenceScheduler s(c);
            auto r=simulate_events({{"a",10,2},{"b",20,3}},graph.view(),{},c,s);
            require(r.makespan==23 && r.busy_ticks==std::vector<Tick>({5,0,0,0}), "idle time counted as useful work");
        }},
        {"maximum tick succeeds and overflowing finish fails", [] {
            FixtureGraph g(1); const auto max=std::numeric_limits<Tick>::max(); ReferenceScheduler s({});
            require(simulate_events({{"a",max-2,2}},g.view(),{}, {},s).makespan==max, "maximum time rejected");
            ReferenceScheduler bad({}); EngineStats stats; stats.ready_tasks=9;
            rejects<std::overflow_error>([&]{simulate_events({{"a",max,1}},g.view(),{}, {},bad,&stats);});
            require(stats.ready_tasks==0, "failed run published partial counters");
            FixtureGraph parallel(2); ReferenceScheduler parallel_scheduler({});
            auto r=simulate_events({{"a",0,max},{"b",0,max}},parallel.view(),{}, {},parallel_scheduler);
            require(r.makespan==max && r.busy_ticks==std::vector<Tick>({max,max}), "parallel busy ticks overflowed");
        }},
        {"invalid input sizes durations ranks and configuration", [] {
            FixtureGraph g(1); ReferenceScheduler s({});
            rejects<std::invalid_argument>([&]{simulate_events({{"a",-1,1}},g.view(),{}, {},s);});
            rejects<std::invalid_argument>([&]{simulate_events({{"a",0,0}},g.view(),{}, {},s);});
            rejects<std::invalid_argument>([&]{simulate_events({{"a",0,-1}},g.view(),{}, {},s);});
            rejects<std::invalid_argument>([&]{simulate_events({},g.view(),{}, {},s);});
            rejects<std::invalid_argument>([&]{simulate_events({{"a",0,1}},g.view(),{1,2}, {},s);});
            rejects<std::invalid_argument>([&]{simulate_events({{"a",0,1}},g.view(),{-1}, {},s);});
            rejects<std::invalid_argument>([&]{simulate_events({{"a",0,1}},g.view(),{}, {2,Policy::critical_path,Distribution::central},s);});
            rejects<std::invalid_argument>([&]{simulate_events({{"a",0,1}},g.view(),{}, {0,Policy::fifo,Distribution::central},s);});
            rejects<std::invalid_argument>([&]{simulate_events({{"a",0,1}},DependencyView{g.degrees,{}},{}, {},s);});
        }},
        {"invalid scheduler assignment batches are rejected", [] {
            FixtureGraph graph(2);
            const std::vector<std::vector<Assignment>> invalid = {
                {{0,0,false},{0,1,false}}, {{0,0,false},{1,0,false}},
                {{2,0,false}}, {{0,2,false}}, {{0,0,true}}, {}, {{0,0,false}}
            };
            for (const auto& batch : invalid) {
                ScriptedScheduler s({batch});
                rejects<std::logic_error>([&]{simulate_events({{"a",0,1},{"b",0,1}},graph.view(),{}, {},s);});
            }
        }},
        {"blocked completed and busy assignments are rejected", [] {
            FixtureGraph g(2); const std::vector<TaskSpec> tasks={{"a",0,4},{"b",1,1}};
            ScriptedScheduler unreleased({{{1,0,false}}});
            rejects<std::logic_error>([&]{simulate_events(tasks,g.view(),{}, {},unreleased);});
            ScriptedScheduler busy({{{0,0,false}},{{1,0,false}}});
            rejects<std::logic_error>([&]{simulate_events(tasks,g.view(),{}, {},busy);});
            ScriptedScheduler completed({{{0,0,false}},{{0,0,false}}});
            rejects<std::logic_error>([&]{simulate_events({{"a",0,1},{"b",1,1}},g.view(),{}, {},completed);});
        }},
        {"static owner queues may leave other units idle", [] {
            FixtureGraph graph(3); RunConfig c{2,Policy::fifo,Distribution::static_round_robin}; ReferenceScheduler s(c);
            auto r=simulate_events({{"a",0,4},{"b",0,1},{"c",0,1}},graph.view(),{},c,s);
            require(r.makespan==5 && r.tasks[2].start==4 && r.tasks[2].initial_owner==0, "static owner timing wrong");
            ScriptedScheduler bad({{{0,1,false},{1,0,false}}});
            rejects<std::logic_error>([&]{simulate_events({{"a",0,4},{"b",0,1},{"c",0,1}},graph.view(),{},c,bad);});
        }},
        {"scripted steal records original owner and checks flag", [] {
            FixtureGraph graph(3); RunConfig c{2,Policy::fifo,Distribution::work_stealing};
            ScriptedScheduler s({{{0,0,false},{1,1,false}},{{2,1,true}}});
            auto r=simulate_events({{"a",0,4},{"b",0,1},{"c",0,1}},graph.view(),{},c,s);
            require(r.makespan==4 && r.tasks[2].stolen && r.tasks[2].initial_owner==0 && r.tasks[2].unit==1, "steal record wrong");
            ScriptedScheduler bad({{{0,0,false},{1,1,false}},{{2,1,false}}});
            rejects<std::logic_error>([&]{simulate_events({{"a",0,4},{"b",0,1},{"c",0,1}},graph.view(),{},c,bad);});
        }},
        {"bad graph metadata fails without hanging", [] {
            FixtureGraph cycle(2,{{0,1},{1,0}}); ReferenceScheduler s({});
            rejects<std::runtime_error>([&]{simulate_events({{"a",0,1},{"b",0,1}},cycle.view(),{}, {},s);});
            FixtureGraph invalid(2); invalid.successors[0].push_back(2); ReferenceScheduler outside({});
            rejects<std::invalid_argument>([&]{simulate_events({{"a",0,1},{"b",0,2}},invalid.view(),{}, {},outside);});
            invalid.successors[0][0]=1; ReferenceScheduler mismatch({});
            rejects<std::invalid_argument>([&]{simulate_events({{"a",0,1},{"b",0,2}},invalid.view(),{}, {},mismatch);});
        }},
        {"960 seeded DAG/configuration cases match tick oracle", randomized_differential}
    };
    std::size_t failed=0;
    for (const auto& test : tests) {
        try { test.second(); output << "PASS engine: " << test.first << '\n'; }
        catch (const std::exception& error) { ++failed; output << "FAIL engine: " << test.first << ": " << error.what() << '\n'; }
    }
    output << tests.size()-failed << '/' << tests.size() << " engine tests passed (test-only graph and scheduler adapters).\n";
    return failed==0 ? 0 : 1;
}

void write_engine_checkpoint(const std::string& directory) {
    FixtureGraph graph(6,six_edges);
    RunConfig config{2,Policy::fifo,Distribution::central}; ReferenceScheduler scheduler(config);
    EngineStats stats;
    const auto result=simulate_events(six_tasks,graph.view(),{},config,scheduler,&stats);
    std::ofstream trace;
    trace.exceptions(std::ios::failbit | std::ios::badbit);
    trace.open(directory+"/engine_fixture_trace.csv");
    trace << "task_id,ready,start,finish,unit,initial_owner,stolen\n";
    for (const auto& row : result.tasks)
        trace << six_tasks[row.task].external_id << ',' << row.ready << ',' << row.start << ',' << row.finish
              << ',' << row.unit << ',' << row.initial_owner << ',' << row.stolen << '\n';
    trace.close();
    std::ofstream summary;
    summary.exceptions(std::ios::failbit | std::ios::badbit);
    summary.open(directory+"/engine_checkpoint.txt");
    summary << "Isolated engine checkpoint: real event engine, test-only graph and FIFO scheduler.\n"
            << "This is not the full project benchmark or CSV parsing pipeline.\n"
            << "Six-task FIFO makespan: " << result.makespan << " ticks\n"
            << "Busy ticks: " << result.busy_ticks[0] << ", " << result.busy_ticks[1] << '\n'
            << "Event pushes/pops: " << stats.events.pushes << '/' << stats.events.pops << '\n'
            << "Event comparisons/swaps/peak size: " << stats.events.comparisons << '/' << stats.events.swaps << '/' << stats.events.peak_size << '\n'
            << "Timestamp batches: " << stats.timestamp_batches << '\n'
            << "Dispatch calls: " << stats.dispatch_calls << '\n'
            << "Successor visits: " << stats.successor_visits << '\n'
            << "Tasks readied: " << stats.ready_tasks << '\n';
    summary.close();
}
} // namespace gpu
