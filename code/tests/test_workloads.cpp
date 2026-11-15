// Tests for the owner-skew workload generator and its saved fixtures.
// Expected numbers are derived analytically in the comments (heavy = 10, light = 1).
#include "gpu/scheduler.hpp"
#include "gpu/self_test.hpp"
#include "gpu/workloads.hpp"
#include "oracle_sim.hpp"
#include <fstream>
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

const std::size_t kSizes[] = {100, 1000, 10000};
const std::size_t kUnits[] = {1, 2, 4, 8};

std::vector<std::string> split(const std::string& text, char separator) {
    std::vector<std::string> parts;
    std::string part;
    std::istringstream in(text);
    while (std::getline(in, part, separator)) parts.push_back(part);
    return parts;
}
OwnerSkewSettings settings_for(std::size_t n, std::size_t p) {
    OwnerSkewSettings s;
    s.tasks = n;
    s.units = p;
    return s;
}
std::size_t heavy_count(std::size_t n, std::size_t p) { return (n + p - 1) / p; }  // ids 0, P, 2P, ...

void exact_small_example() {
    // N=5, P=2: ids 0,2,4 are owned by unit 0 -> heavy; ids 1,3 -> light.
    OwnerSkewSettings s = settings_for(5, 2);
    require(owner_skew_csv(s) ==
            "task_id,release_time,duration,dependencies\n"
            "T0,0,10,\nT1,0,1,\nT2,0,10,\nT3,0,1,\nT4,0,10,\n", "small example text");
    s.heavy_duration = 7;
    s.light_duration = 3;
    require(owner_skew_csv(s).find("T0,0,7,\nT1,0,3,\n") != std::string::npos, "custom durations ignored");
    require(owner_skew_name(100, 2) == "owner_skew_N100_P2", "file stem");
}
void rejects_invalid_settings() {
    throws<std::invalid_argument>([] { owner_skew_csv(settings_for(0, 2)); });
    throws<std::invalid_argument>([] { owner_skew_csv(settings_for(5, 0)); });
    throws<std::invalid_argument>([] {
        OwnerSkewSettings s = settings_for(5, 2); s.heavy_duration = 0; owner_skew_csv(s); });
    throws<std::invalid_argument>([] {
        OwnerSkewSettings s = settings_for(5, 2); s.light_duration = -1; owner_skew_csv(s); });
}
void generated_files_follow_input_format() {
    for (std::size_t n : kSizes)
        for (std::size_t p : kUnits) {
            const std::vector<std::string> lines = split(owner_skew_csv(settings_for(n, p)), '\n');
            require(lines.size() == n + 1 && lines[0] == "task_id,release_time,duration,dependencies",
                    "header or row count");
            long long total = 0;
            std::size_t heavy = 0;
            for (std::size_t i = 0; i < n; ++i) {
                const std::string& line = lines[i + 1];
                const std::string heavy_row = "T" + std::to_string(i) + ",0,10,";
                const std::string light_row = "T" + std::to_string(i) + ",0,1,";
                // Exactly four fields (three commas), unique id T<i>, release 0, no dependencies.
                if (i % p == 0) { require(line == heavy_row, "owner-0 task must be heavy"); ++heavy; total += 10; }
                else            { require(line == light_row, "other tasks must be light"); total += 1; }
            }
            require(heavy == heavy_count(n, p), "heavy task count");
            // Total work = 10 * ceil(N/P) + (N - ceil(N/P)).
            const long long expected = 10 * static_cast<long long>(heavy_count(n, p)) +
                                       static_cast<long long>(n - heavy_count(n, p));
            require(total == expected, "total work");
        }
}
std::string read_whole(const std::string& path, bool& found) {
    std::ifstream in(path, std::ios::binary);
    found = static_cast<bool>(in);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}
std::string fixtures_dir() {
    // run.sh runs from code/; also allow the repository root.
    for (const char* dir : {"data/workloads", "code/data/workloads"}) {
        bool found = false;
        read_whole(std::string(dir) + "/owner_skew_settings.txt", found);
        if (found) return dir;
    }
    throw std::runtime_error("saved workloads not found: run from code/ or the repository root");
}
void saved_files_match_generator() {
    const std::string dir = fixtures_dir();
    for (std::size_t n : kSizes)
        for (std::size_t p : kUnits) {
            bool found = false;
            const std::string saved = read_whole(dir + "/" + owner_skew_name(n, p) + ".csv", found);
            require(found, "saved workload file missing");
            require(saved == owner_skew_csv(settings_for(n, p)), "saved workload differs from generator");
        }
    bool found = false;
    require(read_whole(dir + "/owner_skew_settings.txt", found) == owner_skew_settings_text(),
            "saved settings file differs from generator");
}
// Independent tasks (no dependencies): rank = duration.
std::vector<oracle::OracleTask> tasks_from_csv(const std::string& csv) {
    std::vector<oracle::OracleTask> tasks;
    const std::vector<std::string> lines = split(csv, '\n');
    for (std::size_t i = 1; i < lines.size(); ++i) {
        const std::vector<std::string> fields = split(lines[i], ',');
        const Tick duration = std::stoll(fields.at(2));
        tasks.push_back(oracle::OracleTask{duration, duration, {}});
    }
    return tasks;
}
void skew_separates_static_from_central() {
    // Sizes 100 and 1000 only: the O(N^2) test simulator is too slow for 10,000.
    for (std::size_t n : {100u, 1000u})
        for (std::size_t p : kUnits) {
            const auto tasks = tasks_from_csv(owner_skew_csv(settings_for(n, p)));
            const Tick heavy = static_cast<Tick>(heavy_count(n, p));
            const Tick work = 10 * heavy + static_cast<Tick>(n) - heavy;
            StaticFifoScheduler stat(p);
            CentralFifoScheduler central;
            const auto static_trace = oracle::run_tasks(tasks, stat, p);
            const auto central_trace = oracle::run_tasks(tasks, central, p);
            // Static: every task runs on its owner (id mod P) ...
            for (std::size_t i = 0; i < n; ++i)
                require(static_trace[i].unit == i % p, "static must run each task on its owner");
            // ... and unit 0 serially executes all heavy tasks: makespan = 10 * ceil(N/P).
            require(oracle::makespan_of(static_trace) == 10 * heavy, "static makespan = 10*ceil(N/P)");
            const Tick central_span = oracle::makespan_of(central_trace);
            // Any schedule needs max(ceil(W/P), longest task); list scheduling stays within W/P + longest.
            const Tick lower = std::max<Tick>((work + static_cast<Tick>(p) - 1) / static_cast<Tick>(p), 10);
            const Tick upper = work / static_cast<Tick>(p) + 10;
            if (p == 1) {
                require(central_span == work && oracle::makespan_of(static_trace) == work,
                        "P=1: both strategies must equal total work");
            } else {
                require(central_span >= lower && central_span <= upper, "central makespan outside list-scheduling bounds");
                require(central_span < oracle::makespan_of(static_trace), "skew must make static slower than central");
            }
        }
}
} // namespace

std::vector<std::pair<const char*, std::function<void()>>> workload_tests() {
    return {
        {"owner-skew generator exact small example", exact_small_example},
        {"owner-skew generator rejects invalid settings", rejects_invalid_settings},
        {"owner-skew files follow input format and pattern", generated_files_follow_input_format},
        {"owner-skew saved fixtures match generator", saved_files_match_generator},
        {"owner-skew separates static from central (N=100,1000)", skew_separates_static_from_central},
    };
}
} // namespace gpu
