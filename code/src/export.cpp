#include "gpu/export.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace gpu {
namespace {

std::string fixed(double value, int decimals) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(decimals) << value;
    return out.str();
}

std::string optional_field(const std::optional<double>& value) {
    return value ? fixed(*value, 6) : std::string("N/A");
}

std::string xml_escape(const std::string& text) {
    std::string out;
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out += c;
        }
    }
    return out;
}

// Whole numbers print without decimals, others with two.
std::string tick_label(double value) {
    if (value == std::floor(value) && std::fabs(value) < 1e15) return fixed(value, 0);
    return fixed(value, 2);
}

} // namespace

std::string csv_escape(const std::string& text) {
    if (text.find_first_of(",\"\r\n") == std::string::npos) return text;
    std::string quoted = "\"";
    for (char c : text) {
        if (c == '"') quoted += '"';  // double the quote
        quoted += c;
    }
    quoted += '"';
    return quoted;
}

std::string policy_name(Policy policy) {
    switch (policy) {
        case Policy::fifo: return "fifo";
        case Policy::shortest_job_first: return "sjf";
        case Policy::critical_path: return "cp";
    }
    throw std::invalid_argument("unknown policy");
}

std::string distribution_name(Distribution distribution) {
    switch (distribution) {
        case Distribution::central: return "central";
        case Distribution::static_round_robin: return "static";
        case Distribution::work_stealing: return "stealing";
    }
    throw std::invalid_argument("unknown distribution");
}

std::string config_label(Policy policy, Distribution distribution) {
    return distribution_name(distribution) + "_" + policy_name(policy);
}

std::string results_csv(const std::vector<ExperimentRow>& rows) {
    std::ostringstream out;
    out << "workload,compute_units,policy,distribution,config,makespan,mean_ready_wait,"
           "p95_ready_wait,mean_turnaround,utilization,busy_time_cv\n";
    for (const ExperimentRow& row : rows) {
        out << csv_escape(row.workload) << ',' << row.compute_units << ','
            << policy_name(row.policy) << ',' << distribution_name(row.distribution) << ','
            << config_label(row.policy, row.distribution) << ',' << row.metrics.makespan << ','
            << optional_field(row.metrics.mean_ready_wait) << ','
            << optional_field(row.metrics.p95_ready_wait) << ','
            << optional_field(row.metrics.mean_turnaround) << ','
            << optional_field(row.metrics.utilization) << ','
            << optional_field(row.metrics.busy_time_cv) << '\n';
    }
    return out.str();
}

std::string svg_bar_chart(const std::string& title, const std::string& y_axis_label,
                          const std::vector<Bar>& bars) {
    double max_value = 0.0;
    for (const Bar& bar : bars) {
        if (!std::isfinite(bar.value) || bar.value < 0.0)
            throw std::invalid_argument("bar values must be finite and non-negative");
        max_value = std::max(max_value, bar.value);
    }
    if (max_value <= 0.0) max_value = 1.0;  // all zero (or empty): avoid dividing by zero

    // Fixed layout so output is deterministic and easy to reason about.
    const double left = 70, right = 20, top = 50, bottom = 90;
    const double plot_h = 300, slot = 80, bar_w = 50;
    const double width = std::max(320.0, left + slot * static_cast<double>(bars.size()) + right);
    const double height = top + plot_h + bottom;
    const double base_y = top + plot_h;

    std::ostringstream svg;
    svg.imbue(std::locale::classic());
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << fixed(width, 0) << "\" height=\""
        << fixed(height, 0) << "\" viewBox=\"0 0 " << fixed(width, 0) << ' ' << fixed(height, 0)
        << "\" font-family=\"sans-serif\" font-size=\"12\">\n";
    svg << "<rect width=\"" << fixed(width, 0) << "\" height=\"" << fixed(height, 0)
        << "\" fill=\"#ffffff\"/>\n";
    svg << "<text x=\"" << fixed(width / 2, 0) << "\" y=\"26\" text-anchor=\"middle\" "
        << "font-size=\"16\" fill=\"#222222\">" << xml_escape(title) << "</text>\n";
    const double mid_y = top + plot_h / 2;
    svg << "<text x=\"16\" y=\"" << fixed(mid_y, 0) << "\" transform=\"rotate(-90 16 "
        << fixed(mid_y, 0) << ")\" text-anchor=\"middle\" fill=\"#222222\">"
        << xml_escape(y_axis_label) << "</text>\n";

    if (bars.empty()) {
        svg << "<text x=\"" << fixed(width / 2, 0) << "\" y=\"" << fixed(mid_y, 0)
            << "\" text-anchor=\"middle\" fill=\"#666666\">no data</text>\n</svg>\n";
        return svg.str();
    }

    // Horizontal grid lines and y tick labels at 0, 25, 50, 75, 100 % of the maximum.
    for (int i = 0; i <= 4; ++i) {
        const double y = base_y - plot_h * i / 4.0;
        svg << "<line x1=\"" << fixed(left, 0) << "\" y1=\"" << fixed(y, 2) << "\" x2=\""
            << fixed(width - right, 0) << "\" y2=\"" << fixed(y, 2) << "\" stroke=\""
            << (i == 0 ? "#222222" : "#dddddd") << "\"/>\n";
        svg << "<text x=\"" << fixed(left - 6, 0) << "\" y=\"" << fixed(y + 4, 2)
            << "\" text-anchor=\"end\" fill=\"#444444\">" << tick_label(max_value * i / 4.0)
            << "</text>\n";
    }
    svg << "<line x1=\"" << fixed(left, 0) << "\" y1=\"" << fixed(top, 0) << "\" x2=\""
        << fixed(left, 0) << "\" y2=\"" << fixed(base_y, 0) << "\" stroke=\"#222222\"/>\n";

    for (std::size_t i = 0; i < bars.size(); ++i) {
        const double h = bars[i].value / max_value * plot_h;
        const double x = left + slot * static_cast<double>(i) + (slot - bar_w) / 2;
        const double cx = x + bar_w / 2;
        svg << "<rect x=\"" << fixed(x, 2) << "\" y=\"" << fixed(base_y - h, 2) << "\" width=\""
            << fixed(bar_w, 2) << "\" height=\"" << fixed(h, 2) << "\" fill=\"#4472c4\"/>\n";
        svg << "<text x=\"" << fixed(cx, 2) << "\" y=\"" << fixed(base_y - h - 5, 2)
            << "\" text-anchor=\"middle\" fill=\"#222222\">" << tick_label(bars[i].value) << "</text>\n";
        svg << "<text x=\"" << fixed(cx, 2) << "\" y=\"" << fixed(base_y + 16, 2)
            << "\" transform=\"rotate(-30 " << fixed(cx, 2) << ' ' << fixed(base_y + 16, 2)
            << ")\" text-anchor=\"end\" fill=\"#222222\">" << xml_escape(bars[i].label) << "</text>\n";
    }
    svg << "</svg>\n";
    return svg.str();
}

std::string makespan_chart_svg(const std::vector<ExperimentRow>& rows, const std::string& title) {
    std::vector<Bar> bars;
    for (const ExperimentRow& row : rows)
        bars.push_back(Bar{config_label(row.policy, row.distribution) + " P=" +
                               std::to_string(row.compute_units),
                           static_cast<double>(row.metrics.makespan)});
    return svg_bar_chart(title, "makespan (ticks)", bars);
}

std::string busy_time_chart_svg(const ExperimentRow& row) {
    std::vector<Bar> bars;
    for (std::size_t unit = 0; unit < row.busy_ticks.size(); ++unit)
        bars.push_back(Bar{"Unit " + std::to_string(unit), static_cast<double>(row.busy_ticks[unit])});
    const std::string title = "Useful busy time per unit - " + row.workload + ", " +
                              config_label(row.policy, row.distribution) +
                              ", P=" + std::to_string(row.compute_units);
    return svg_bar_chart(title, "busy time (ticks)", bars);
}

void write_text_file(const std::string& path, const std::string& content) {
    // The parent directory must already exist (run.sh creates results/). std::filesystem
    // is deliberately not used: on Windows/Git Bash it can fail to load at startup.
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) throw std::runtime_error("cannot open for writing: " + path);
    file << content;
    file.flush();
    if (!file) throw std::runtime_error("write failed: " + path);
}

namespace {
std::string safe_name(const std::string& text) {
    std::string out;
    for (char ch : text) {
        const bool ok = (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') ||
                        (ch >= 'a' && ch <= 'z') || ch == '_';
        out += ok ? ch : '_';
    }
    return out;
}
} // namespace

std::vector<std::pair<std::string, std::string>> result_files(const std::vector<ExperimentRow>& rows,
                                                              const ResultFileOptions& options) {
    std::vector<std::pair<std::string, std::string>> files;
    files.emplace_back("results.csv", results_csv(rows));

    // One makespan chart per distinct (workload, P), in order of first appearance.
    std::vector<std::pair<std::string, std::size_t>> groups;
    for (const ExperimentRow& row : rows) {
        const std::pair<std::string, std::size_t> key{row.workload, row.compute_units};
        if (std::find(groups.begin(), groups.end(), key) == groups.end()) groups.push_back(key);
    }
    for (const auto& group : groups) {
        std::vector<ExperimentRow> members;
        for (const ExperimentRow& row : rows)
            if (row.workload == group.first && row.compute_units == group.second) members.push_back(row);
        files.emplace_back("makespan_" + safe_name(group.first) + "_P" + std::to_string(group.second) + ".svg",
                           makespan_chart_svg(members, "Makespan - " + group.first + ", P=" +
                                                           std::to_string(group.second)));
    }

    for (const ExperimentRow& row : rows)
        if (row.workload.compare(0, options.busy_chart_workload_prefix.size(),
                                 options.busy_chart_workload_prefix) == 0)
            files.emplace_back("busy_" + safe_name(row.workload) + "_" +
                                   config_label(row.policy, row.distribution) + "_P" +
                                   std::to_string(row.compute_units) + ".svg",
                               busy_time_chart_svg(row));

    for (std::size_t i = 0; i < files.size(); ++i)
        for (std::size_t j = i + 1; j < files.size(); ++j)
            if (files[i].first == files[j].first)
                throw std::invalid_argument("duplicate result file name: " + files[i].first);
    return files;
}

void write_result_files(const std::vector<ExperimentRow>& rows, const std::string& path_prefix,
                        const ResultFileOptions& options) {
    for (const auto& file : result_files(rows, options)) write_text_file(path_prefix + file.first, file.second);
}

} // namespace gpu
