#include "gpu/hash_parser_metrics_tests.hpp"

#include "gpu/contracts.hpp"
#include "gpu/ds/id_hash_table.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include <unordered_map> // test-only reference container, never used in production code

namespace gpu {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

// ---------------------------------------------------------------- hash table
void hash_basic() {
    IdHashTable table;
    require(table.size() == 0 && !table.find("A"), "new table must be empty");
    require(table.insert("A", 0) && table.insert("B", 1) && table.insert("", 2), "inserts failed");
    require(table.size() == 3, "wrong size");
    require(table.find("A") == 0 && table.find("B") == 1 && table.find("") == 2, "wrong lookup");
    require(!table.find("a") && !table.find("AB"), "lookup must be exact");
}
void hash_duplicates() {
    IdHashTable table;
    require(table.insert("X", 5), "first insert");
    require(!table.insert("X", 9), "duplicate insert must return false");
    require(table.find("X") == 5 && table.size() == 1, "duplicate must not replace or grow");
}
void hash_forced_collisions() {
    for (std::size_t constant : {std::size_t(0), std::size_t(42), std::numeric_limits<std::size_t>::max()}) {
        IdHashTable table(2, [constant](const std::string&) { return constant; });
        for (std::size_t i = 0; i < 300; ++i)
            require(table.insert("k" + std::to_string(i), i), "colliding insert failed");
        require(table.size() == 300, "size after collisions");
        require(table.load_factor() <= 0.75, "load factor above limit");
        for (std::size_t i = 0; i < 300; ++i)
            require(table.find("k" + std::to_string(i)) == i, "colliding lookup wrong");
        require(!table.find("k300") && !table.find("missing"), "missing key found among collisions");
        require(!table.insert("k17", 99) && table.find("k17") == 17, "duplicate among collisions");
    }
}
void hash_rehash_keeps_everything() {
    for (std::size_t initial : {std::size_t(0), std::size_t(1), std::size_t(3), std::size_t(8)}) {
        IdHashTable table(initial);
        for (std::size_t i = 0; i < 5000; ++i) {
            require(table.insert("task_" + std::to_string(i), i), "insert failed");
            require(table.load_factor() <= 0.75, "load factor exceeded 0.75");
        }
        for (std::size_t i = 0; i < 5000; ++i)
            require(table.find("task_" + std::to_string(i)) == i, "entry lost across rehash");
    }
}
void hash_random_versus_reference() {
    std::mt19937 rng(2026);
    IdHashTable table(1);
    std::unordered_map<std::string, TaskId> reference;
    for (int step = 0; step < 20000; ++step) {
        const std::string key = "k" + std::to_string(rng() % 700);
        if (rng() % 2 == 0) {
            const TaskId value = rng();
            const bool inserted = reference.emplace(key, value).second;
            require(table.insert(key, value) == inserted, "insert result disagrees with reference");
        } else {
            const auto it = reference.find(key);
            const std::optional<TaskId> got = table.find(key);
            require(got.has_value() == (it != reference.end()), "presence disagrees with reference");
            if (got) require(*got == it->second, "value disagrees with reference");
        }
        require(table.size() == reference.size() && table.load_factor() <= 0.75, "size or load disagrees");
    }
}
void hash_reserve() {
    IdHashTable table(1);
    table.insert("keep", 7);
    table.reserve(1000);
    require(table.find("keep") == 7 && table.size() == 1, "reserve lost an entry");
    for (std::size_t i = 0; i < 1000; ++i) table.insert("r" + std::to_string(i), i);
    require(table.load_factor() <= 0.75, "load after reserve");
    for (std::size_t i = 0; i < 1000; ++i) require(table.find("r" + std::to_string(i)) == i, "lost after reserve");
    table.reserve(0);
    table.reserve(10); // shrinking requests are ignored
    require(table.size() == 1001 && table.find("keep") == 7, "reserve below size changed table");
}
void hash_default_hash_is_total() {
    IdHashTable table;
    const std::string odd("a\0b", 3); // embedded NUL and high-bit bytes
    require(table.insert(odd, 1) && table.insert(std::string("\xFF\xFE"), 2), "odd keys");
    require(table.find(odd) == 1 && table.find(std::string("\xFF\xFE")) == 2 && !table.find("a"), "odd lookup");
}

// -------------------------------------------------------------------- parser
struct TempFile {
    std::string path;
    TempFile(const std::string& name, const std::string& content) : path("results/parser_test_" + name + ".csv") {
        std::ofstream out(path, std::ios::binary);
        out << content;
        require(static_cast<bool>(out), "cannot write temp file");
    }
    ~TempFile() { std::remove(path.c_str()); }
};
const char* const header = "task_id,release_time,duration,dependencies\n";

// Parses `content` and requires an error whose message contains every fragment.
void expect_error(const std::string& name, const std::string& content,
                  const std::vector<std::string>& fragments) {
    TempFile file(name, content);
    try {
        parse_csv(file.path);
    } catch (const std::exception& error) {
        const std::string message = error.what();
        require(message.find(file.path) != std::string::npos, "error must include the path");
        for (const auto& fragment : fragments)
            if (message.find(fragment) == std::string::npos)
                throw std::runtime_error("error for '" + name + "' lacks '" + fragment + "': " + message);
        return;
    }
    throw std::runtime_error("parser accepted invalid input: " + name);
}

void parser_six_tasks() {
    const ParsedInput in = parse_csv("data/fixtures/six_tasks.csv");
    const std::vector<std::string> ids = {"A", "B", "C", "D", "E", "F"};
    const std::vector<Tick> durations = {2, 3, 6, 2, 4, 1};
    require(in.tasks.size() == 6, "task count");
    for (std::size_t i = 0; i < 6; ++i)
        require(in.tasks[i].external_id == ids[i] && in.tasks[i].duration == durations[i] &&
                    in.tasks[i].release == 0, "task fields or row order wrong");
    const std::vector<EdgePair> edges = {{0, 3}, {1, 3}, {2, 4}, {3, 5}, {4, 5}};
    require(in.edges.size() == edges.size(), "edge count");
    for (std::size_t i = 0; i < edges.size(); ++i)
        require(in.edges[i].from == edges[i].from && in.edges[i].to == edges[i].to, "edge wrong");
}
void parser_fixtures() {
    const ParsedInput forward = parse_csv("data/fixtures/forward_reference.csv"); // B depends on later A
    require(forward.tasks.size() == 2 && forward.tasks[0].external_id == "B" &&
                forward.tasks[1].external_id == "A", "forward reference rows");
    require(forward.edges.size() == 1 && forward.edges[0].from == 1 && forward.edges[0].to == 0,
            "forward reference edge must be A -> B in dense indices");
    const ParsedInput empty = parse_csv("data/fixtures/empty.csv");
    require(empty.tasks.empty() && empty.edges.empty(), "header-only file is a valid empty workload");
    const ParsedInput sim = parse_csv("data/fixtures/simultaneous.csv");
    require(sim.tasks.size() == 2 && sim.tasks[1].release == 3, "release time parsed");
    // Cycles are the graph validator's job: the parser only reports the edges.
    const ParsedInput cycle = parse_csv("data/fixtures/invalid_cycle.csv");
    require(cycle.tasks.size() == 2 && cycle.edges.size() == 2, "parser must not reject cycles");
}
void parser_formatting() {
    TempFile crlf("crlf", "task_id,release_time,duration,dependencies\r\n A , 1 , 2 , \r\nB,0,3, A ; C \r\nC,0,1,\r\n\r\n\n");
    const ParsedInput in = parse_csv(crlf.path);
    require(in.tasks.size() == 3 && in.tasks[0].external_id == "A" && in.tasks[0].release == 1, "CRLF/trim");
    require(in.edges.size() == 2 && in.edges[0].from == 0 && in.edges[0].to == 1 &&
                in.edges[1].from == 2 && in.edges[1].to == 1, "dependency list with spaces");
    TempFile bom("bom", std::string("\xEF\xBB\xBF") + header + "A,0,1,\n");
    require(parse_csv(bom.path).tasks.size() == 1, "BOM must be ignored");
    TempFile no_newline("nonl", std::string(header) + "A,0,1,");
    require(parse_csv(no_newline.path).tasks.size() == 1, "missing final newline");
    TempFile big("big", std::string(header) + "A,0,9223372036854775806,\n");
    require(parse_csv(big.path).tasks[0].duration == 9223372036854775806LL, "large duration within range");
}
void parser_rejects_invalid() {
    expect_error("duplicate", std::string(header) + "A,0,1,\nA,0,2,\n", {"row 3", "task_id", "duplicate"});
    expect_error("unknown", std::string(header) + "A,0,1,Z\n", {"row 2", "dependencies", "unknown", "Z"});
    expect_error("self", std::string(header) + "A,0,1,A\n", {"row 2", "dependencies", "itself"});
    expect_error("dupdep", std::string(header) + "A,0,1,\nB,0,1,A;A\n", {"row 3", "duplicate dependency"});
    expect_error("emptydep", std::string(header) + "A,0,1,\nB,0,1,A;;A\n", {"row 3", "dependencies"});
    expect_error("badint", std::string(header) + "A,0x,1,\n", {"row 2", "release_time", "malformed"});
    expect_error("emptyint", std::string(header) + "A,0,,\n", {"row 2", "duration", "integer"});
    expect_error("negrelease", std::string(header) + "A,-1,1,\n", {"row 2", "release_time", ">= 0"});
    expect_error("zerodur", std::string(header) + "A,0,0,\n", {"row 2", "duration", "> 0"});
    expect_error("negdur", std::string(header) + "A,0,-4,\n", {"row 2", "duration", "> 0"});
    expect_error("overflow", std::string(header) + "A,0,99999999999999999999,\n", {"row 2", "duration", "overflow"});
    expect_error("sumoverflow", std::string(header) + "A,9223372036854775807,1,\n", {"row 2", "overflows"});
    expect_error("fields3", std::string(header) + "A,0,1\n", {"row 2", "4", "found 3"});
    expect_error("fields5", std::string(header) + "A,0,1,,\n", {"row 2", "4", "found 5"});
    expect_error("blank", std::string(header) + "A,0,1,\n\nB,0,1,\n", {"row 3", "4"});
    expect_error("badid", std::string(header) + "A-1,0,1,\n", {"row 2", "task_id"});
    expect_error("emptyid", std::string(header) + ",0,1,\n", {"row 2", "task_id"});
    expect_error("quoted", std::string(header) + "\"A\",0,1,\n", {"row 2", "task_id"});
    expect_error("header", "id,release,duration,deps\nA,0,1,\n", {"row 1", "header"});
    expect_error("noheader", "", {"row 1", "header"});
    try {
        parse_csv("results/does_not_exist.csv");
        throw std::runtime_error("missing file accepted");
    } catch (const std::runtime_error& error) {
        require(std::string(error.what()).find("does_not_exist.csv") != std::string::npos, "missing file message");
    }
}
void parser_many_rows_use_rehash() {
    std::string content = header;
    for (int i = 0; i < 2000; ++i)
        content += "t" + std::to_string(i) + ",0,1," + (i == 0 ? "" : "t" + std::to_string(i - 1)) + "\n";
    TempFile file("chain", content);
    const ParsedInput in = parse_csv(file.path);
    require(in.tasks.size() == 2000 && in.edges.size() == 1999, "large workload counts");
    require(in.edges[1998].from == 1998 && in.edges[1998].to == 1999, "large workload edge");
}

// ------------------------------------------------------------------- metrics
// Builds a RunResult from the hand-calculated oracle code/data/expected/six_tasks.csv.
RunResult oracle_result(const std::string& policy, const ValidatedWorkload& workload) {
    std::unordered_map<std::string, std::size_t> index;
    for (std::size_t i = 0; i < workload.tasks.size(); ++i) index[workload.tasks[i].external_id] = i;
    RunResult result;
    result.tasks.resize(workload.tasks.size());
    result.busy_ticks.assign(2, 0);
    std::ifstream in("data/expected/six_tasks.csv");
    require(static_cast<bool>(in), "cannot open oracle file");
    std::string line;
    std::getline(in, line); // header
    std::size_t rows = 0;
    while (std::getline(in, line)) {
        std::stringstream fields(line);
        std::string p, unit, id, ready, start, finish;
        std::getline(fields, p, ','); std::getline(fields, unit, ','); std::getline(fields, id, ',');
        std::getline(fields, ready, ','); std::getline(fields, start, ','); std::getline(fields, finish, ',');
        if (p != policy) continue;
        const std::size_t task = index.at(id), u = std::stoul(unit);
        result.tasks[task] = {task, std::stoll(ready), std::stoll(start), std::stoll(finish), u, 0, false};
        result.busy_ticks[u] += workload.tasks[task].duration;
        result.makespan = std::max(result.makespan, result.tasks[task].finish);
        ++rows;
    }
    require(rows == workload.tasks.size(), "oracle policy rows missing");
    return result;
}
ValidatedWorkload six_task_workload() {
    ValidatedWorkload workload; // the graph is not needed by compute_metrics
    workload.tasks = parse_csv("data/fixtures/six_tasks.csv").tasks;
    return workload;
}
void metrics_hand_oracle() {
    const ValidatedWorkload workload = six_task_workload();
    for (const char* policy : {"fifo", "sjf"}) {
        const Metrics m = compute_metrics(workload, oracle_result(policy, workload));
        require(m.makespan == 13, "FIFO/SJF makespan must be 13");
        require(m.mean_ready_wait && near(*m.mean_ready_wait, 2.0 / 6.0), "FIFO/SJF mean wait 2/6");
        require(m.utilization && near(*m.utilization, 18.0 / 26.0), "FIFO/SJF utilization 18/26");
        require(m.mean_turnaround && near(*m.mean_turnaround, 43.0 / 6.0), "FIFO/SJF turnaround 43/6");
        require(m.p95_ready_wait && near(*m.p95_ready_wait, 2.0), "p95 of six waits is the maximum");
        require(m.busy_time_cv && near(*m.busy_time_cv, 4.0 / 9.0), "busy 13 and 5 give CV 4/9");
    }
    const Metrics cp = compute_metrics(workload, oracle_result("critical_path", workload));
    require(cp.makespan == 11, "CP makespan must be 11");
    require(cp.mean_ready_wait && near(*cp.mean_ready_wait, 3.0 / 6.0), "CP mean wait 3/6");
    require(cp.utilization && near(*cp.utilization, 18.0 / 22.0), "CP utilization 18/22");
    require(cp.mean_turnaround && near(*cp.mean_turnaround, 7.0), "CP turnaround 42/6");
    require(cp.p95_ready_wait && near(*cp.p95_ready_wait, 3.0), "CP p95");
    require(cp.busy_time_cv && near(*cp.busy_time_cv, 2.0 / 9.0), "busy 11 and 7 give CV 2/9");
}
RunResult waits_result(ValidatedWorkload& workload, std::size_t n) {
    // One unit, tasks run back to back with ready time 0, so task i waits i ticks.
    RunResult result;
    result.busy_ticks = {static_cast<Tick>(n)};
    result.makespan = static_cast<Tick>(n);
    for (std::size_t i = 0; i < n; ++i) {
        workload.tasks.push_back({"t" + std::to_string(i), 0, 1});
        result.tasks.push_back({i, 0, static_cast<Tick>(i), static_cast<Tick>(i + 1), 0, 0, false});
    }
    return result;
}
void metrics_nearest_rank_p95() {
    for (auto [n, expected] : {std::pair<std::size_t, double>{1, 0}, {19, 18}, {20, 18}, {21, 19}, {100, 94}}) {
        ValidatedWorkload workload;
        const RunResult result = waits_result(workload, n);
        const Metrics m = compute_metrics(workload, result);
        require(m.p95_ready_wait && near(*m.p95_ready_wait, expected), "nearest-rank p95 wrong");
        require(m.busy_time_cv && near(*m.busy_time_cv, 0.0), "one unit has zero CV");
    }
}
void metrics_empty_and_zero_denominators() {
    ValidatedWorkload empty;
    RunResult none;
    none.busy_ticks = {0, 0};
    const Metrics m = compute_metrics(empty, none);
    require(m.makespan == 0, "empty makespan is zero");
    require(!m.mean_ready_wait && !m.p95_ready_wait && !m.mean_turnaround && !m.utilization && !m.busy_time_cv,
            "empty workload metrics must be N/A");
    RunResult no_units; // zero units: utilization and CV are N/A, per-task values still defined
    ValidatedWorkload one;
    one.tasks.push_back({"A", 0, 4});
    no_units.tasks.push_back({0, 0, 0, 4, 0, 0, false});
    no_units.makespan = 4;
    const Metrics n = compute_metrics(one, no_units);
    require(!n.utilization && !n.busy_time_cv && n.mean_ready_wait && near(*n.mean_ready_wait, 0.0), "zero units");
}
void metrics_rejects_bad_input() {
    ValidatedWorkload workload = six_task_workload();
    RunResult result = oracle_result("fifo", workload);
    RunResult short_result = result;
    short_result.tasks.pop_back();
    bool threw = false;
    try { compute_metrics(workload, short_result); } catch (const std::invalid_argument&) { threw = true; }
    require(threw, "record count mismatch must throw");
    result.tasks[2].start = result.tasks[2].ready - 1;
    threw = false;
    try { compute_metrics(workload, result); } catch (const std::invalid_argument&) { threw = true; }
    require(threw, "start before ready must throw");
}
} // namespace

std::vector<NamedTest> hash_parser_metrics_tests() {
    return {
        {"hash table basic insert and find", hash_basic},
        {"hash table duplicate keeps first value", hash_duplicates},
        {"hash table forced collisions", hash_forced_collisions},
        {"hash table rehash keeps load <= 0.75 and all keys", hash_rehash_keeps_everything},
        {"hash table versus std::unordered_map reference", hash_random_versus_reference},
        {"hash table reserve", hash_reserve},
        {"hash table default hash handles odd bytes", hash_default_hash_is_total},
        {"parser six-task fixture", parser_six_tasks},
        {"parser fixtures: forward reference, empty, cycle", parser_fixtures},
        {"parser CRLF, whitespace, BOM, blank tail", parser_formatting},
        {"parser rejects invalid input with path/row/field", parser_rejects_invalid},
        {"parser large chained workload", parser_many_rows_use_rehash},
        {"metrics match hand-calculated oracle", metrics_hand_oracle},
        {"metrics nearest-rank p95", metrics_nearest_rank_p95},
        {"metrics empty and zero denominators are N/A", metrics_empty_and_zero_denominators},
        {"metrics reject inconsistent run results", metrics_rejects_bad_input},
    };
}
} // namespace gpu
