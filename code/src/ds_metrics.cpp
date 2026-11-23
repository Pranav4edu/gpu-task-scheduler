#include "gpu/ds_metrics.hpp"
#include "gpu/export.hpp"
#include "gpu/scheduler.hpp"
#include <sstream>
#include <stdexcept>

namespace gpu {
namespace {

DsMetricsRow base_row(const std::string& workload, const RunConfig& config, const char* structure,
                      const std::string& instance) {
    DsMetricsRow row;
    row.workload = workload;
    row.config = config_label(config.policy, config.distribution);
    row.compute_units = config.compute_units;
    row.structure = structure;
    row.instance = instance;
    return row;
}

DsMetricsRow deque_row(const std::string& workload, const RunConfig& config, const std::string& instance,
                       const RingDequeStats& stats) {
    DsMetricsRow row = base_row(workload, config, "RingDeque", instance);
    row.pushes = stats.pushes;
    row.pops = stats.pops;
    row.peak_size = stats.peak_size;
    row.resizes = stats.resizes;
    row.elements_copied = stats.elements_copied;
    return row;
}

DsMetricsRow heap_row(const std::string& workload, const RunConfig& config, const HeapStats& stats) {
    DsMetricsRow row = base_row(workload, config, "BinaryHeap", "central_ready_queue");
    row.pushes = stats.pushes;
    row.pops = stats.pops;
    row.peak_size = stats.peak_size;
    row.comparisons = stats.comparisons;
    row.swaps = stats.swaps;
    return row;
}

template<class PerUnitScheduler>
void add_unit_rows(std::vector<DsMetricsRow>& rows, const PerUnitScheduler& scheduler,
                   const std::string& workload, const RunConfig& config) {
    for (UnitId unit = 0; unit < scheduler.units(); ++unit)
        rows.push_back(deque_row(workload, config, "unit_" + std::to_string(unit) + "_queue",
                                 scheduler.queue_stats(unit)));
}

std::string cell(const std::optional<std::size_t>& value) {
    return value ? std::to_string(*value) : std::string("N/A");
}

} // namespace

std::vector<DsMetricsRow> collect_ds_metrics(const Scheduler& scheduler, const std::string& workload,
                                             const RunConfig& config) {
    std::vector<DsMetricsRow> rows;
    if (auto* s = dynamic_cast<const CentralFifoScheduler*>(&scheduler)) {
        rows.push_back(deque_row(workload, config, "central_ready_queue", s->queue_stats()));
    } else if (auto* s = dynamic_cast<const CentralSjfScheduler*>(&scheduler)) {
        rows.push_back(heap_row(workload, config, s->queue_stats()));
    } else if (auto* s = dynamic_cast<const CentralCpScheduler*>(&scheduler)) {
        rows.push_back(heap_row(workload, config, s->queue_stats()));
    } else if (auto* s = dynamic_cast<const StaticFifoScheduler*>(&scheduler)) {
        add_unit_rows(rows, *s, workload, config);
    } else if (auto* s = dynamic_cast<const WorkStealingFifoScheduler*>(&scheduler)) {
        add_unit_rows(rows, *s, workload, config);
        DsMetricsRow steal = base_row(workload, config, "WorkStealing", "steal_search");
        steal.searches = s->steal_stats().searches;
        steal.victim_probes = s->steal_stats().probes;
        steal.steals = s->steal_stats().steals;
        rows.push_back(steal);
    } else {
        throw std::invalid_argument("collect_ds_metrics: unknown scheduler type");
    }
    return rows;
}

std::string ds_metrics_csv(const std::vector<DsMetricsRow>& rows) {
    std::ostringstream out;
    out << "workload,config,compute_units,structure,instance,pushes,pops,peak_size,resizes,"
           "elements_copied,comparisons,swaps,searches,victim_probes,steals\n";
    for (const DsMetricsRow& r : rows)
        out << csv_escape(r.workload) << ',' << csv_escape(r.config) << ',' << r.compute_units << ','
            << r.structure << ',' << r.instance << ',' << cell(r.pushes) << ',' << cell(r.pops) << ','
            << cell(r.peak_size) << ',' << cell(r.resizes) << ',' << cell(r.elements_copied) << ','
            << cell(r.comparisons) << ',' << cell(r.swaps) << ',' << cell(r.searches) << ','
            << cell(r.victim_probes) << ',' << cell(r.steals) << '\n';
    return out.str();
}

} // namespace gpu
