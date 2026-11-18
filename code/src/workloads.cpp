#include "gpu/workloads.hpp"
#include "gpu/export.hpp"
#include <sstream>
#include <stdexcept>
#include <vector>

namespace gpu {
namespace {
const std::vector<std::size_t> kSizes = {100, 1000, 10000};
const std::vector<std::size_t> kUnits = {1, 2, 4, 8};
} // namespace

std::string owner_skew_csv(const OwnerSkewSettings& settings) {
    if (settings.tasks == 0) throw std::invalid_argument("owner-skew workload needs at least one task");
    if (settings.units == 0) throw std::invalid_argument("owner-skew workload needs at least one unit");
    if (settings.heavy_duration <= 0 || settings.light_duration <= 0)
        throw std::invalid_argument("durations must be positive");
    std::ostringstream out;
    out << "task_id,release_time,duration,dependencies\n";
    for (std::size_t i = 0; i < settings.tasks; ++i) {
        const Tick duration = (i % settings.units == 0) ? settings.heavy_duration : settings.light_duration;
        out << 'T' << i << ",0," << duration << ",\n";
    }
    return out.str();
}

std::string owner_skew_name(std::size_t tasks, std::size_t units) {
    return "owner_skew_N" + std::to_string(tasks) + "_P" + std::to_string(units);
}

std::string owner_skew_settings_text() {
    const OwnerSkewSettings defaults;
    std::ostringstream out;
    out << "# Owner-skew workload fixtures (generator: owner_skew_csv in code/src/workloads.cpp)\n"
        << "# Regenerate with: ./build/gpu_sim --write-skew-workloads   (run from the code/ directory)\n"
        << "# The saved CSVs are authoritative; a test checks they match the generator byte for byte.\n"
        << "#\n"
        << "# One workload per (tasks N, units P): 3 sizes x 4 unit counts = 12 files.\n"
        << "# Task i (0-based row) is named T<i>, released at 0, with no dependencies.\n"
        << "# Static round-robin owner of task i = i mod P. Owner-0 tasks are heavy, all others light.\n"
        << "# No random numbers are used, so files are identical on every platform.\n"
        << "# Compare central vs static ONLY at the same P (the pattern changes with P); do not use\n"
        << "# these files for cross-P speedup. With P = 1 central and static are identical.\n"
        << "heavy_duration = " << defaults.heavy_duration << '\n'
        << "light_duration = " << defaults.light_duration << '\n'
        << "tasks =";
    for (std::size_t n : kSizes) out << ' ' << n;
    out << "\nunits =";
    for (std::size_t p : kUnits) out << ' ' << p;
    out << '\n';
    return out.str();
}

void write_owner_skew_fixtures(const std::string& directory) {
    for (std::size_t n : kSizes)
        for (std::size_t p : kUnits) {
            OwnerSkewSettings settings;
            settings.tasks = n;
            settings.units = p;
            write_text_file(directory + "/" + owner_skew_name(n, p) + ".csv", owner_skew_csv(settings));
        }
    write_text_file(directory + "/owner_skew_settings.txt", owner_skew_settings_text());
}

} // namespace gpu
